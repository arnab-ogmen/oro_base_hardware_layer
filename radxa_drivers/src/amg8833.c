/*
 * amg8833.c — AMG8833 / Grid-EYE Linux I2C Driver
 *
 * Platform : Radxa CM5, /dev/i2c-7, Debian 11 Bullseye
 * Datasheet: Panasonic Grid-EYE AMG88 series, Apr. 2017
 *
 * Processing pipeline per frame:
 *   1. Burst I2C read (128 bytes, regs 0x80–0xFF)
 *   2. 12-bit sign extension
 *   3. Convert to °C (× 0.25)
 *   4. Thermistor compensation (× 0.0625)
 *   5. Bad pixel clamping (0–80 °C, replace with neighbour mean)
 *   6. EMA temporal filter (configurable alpha)
 *   7. Min/max tracking
 *   8. Overflow status check
 */

#include "radxa_drivers/drivers/amg8833.h"
#include "radxa_drivers/drivers/amg8833_regs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

/* -----------------------------------------------------------------------
 * Internal handle
 * ----------------------------------------------------------------------- */
struct amg_handle {
    int      fd;               /* Open file descriptor for /dev/i2c-N */
    uint8_t  addr;             /* I2C slave address (0x68 or 0x69)    */
    float    ema_alpha;        /* EMA filter coefficient               */
    float    ema[64];          /* Per-pixel EMA state                  */
    int      ema_init;         /* 0 until first frame seeds the filter */
};

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static uint32_t _now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

/* Write a single byte to a register */
static amg_err_t _reg_write(amg_handle_t *h, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (write(h->fd, buf, 2) != 2)
        return AMG_ERR_WRITE;
    return AMG_OK;
}

/* Read N bytes starting from register `reg` into `dst` */
static amg_err_t _reg_read(amg_handle_t *h, uint8_t reg,
                            uint8_t *dst, size_t len)
{
    /* Send register address */
    if (write(h->fd, &reg, 1) != 1)
        return AMG_ERR_WRITE;

    /* Read response */
    ssize_t n = read(h->fd, dst, len);
    if (n != (ssize_t)len)
        return AMG_ERR_READ;

    return AMG_OK;
}

/* Sign-extend a raw 12-bit pixel value to int16 */
static inline int16_t _sign_extend12(uint16_t raw)
{
    if (raw & 0x0800)
        raw |= 0xF000;   /* extend bit 11 into bits 12–15 */
    return (int16_t)raw;
}

/* Replace out-of-range pixel with cardinal-neighbour mean */
static void _fix_bad_pixel(float *px, int idx, float fallback)
{
    int   r = idx / 8, c = idx % 8;
    float sum = 0.0f;
    int   n   = 0;

    if (r > 0 && px[idx-8] >= AMG_TEMP_MIN && px[idx-8] <= AMG_TEMP_MAX)
        { sum += px[idx-8]; n++; }
    if (r < 7 && px[idx+8] >= AMG_TEMP_MIN && px[idx+8] <= AMG_TEMP_MAX)
        { sum += px[idx+8]; n++; }
    if (c > 0 && px[idx-1] >= AMG_TEMP_MIN && px[idx-1] <= AMG_TEMP_MAX)
        { sum += px[idx-1]; n++; }
    if (c < 7 && px[idx+1] >= AMG_TEMP_MIN && px[idx+1] <= AMG_TEMP_MAX)
        { sum += px[idx+1]; n++; }

    px[idx] = (n > 0) ? (sum / (float)n) : fallback;
}

/* -----------------------------------------------------------------------
 * Public API — Lifecycle
 * ----------------------------------------------------------------------- */

amg_err_t amg_init(const amg_config_t *cfg, amg_handle_t **out)
{
    if (!cfg || !out) return AMG_ERR_NULL;

    amg_handle_t *h = calloc(1, sizeof(amg_handle_t));
    if (!h) return AMG_ERR_NULL;

    h->addr      = cfg->i2c_addr;
    h->ema_alpha = cfg->ema_alpha;
    h->ema_init  = 0;

    /* Open I2C device */
    h->fd = open(cfg->i2c_dev, O_RDWR);
    if (h->fd < 0) {
        fprintf(stderr, "[amg] open(%s): %s\n", cfg->i2c_dev, strerror(errno));
        free(h);
        return AMG_ERR_OPEN;
    }

    /* Set slave address */
    if (ioctl(h->fd, I2C_SLAVE, cfg->i2c_addr) < 0) {
        fprintf(stderr, "[amg] ioctl I2C_SLAVE 0x%02X: %s\n",
                cfg->i2c_addr, strerror(errno));
        close(h->fd);
        free(h);
        return AMG_ERR_IOCTL;
    }

    amg_err_t err;

    /* Initial reset to known state */
    err = _reg_write(h, AMG_REG_RST, AMG_RST_INITIAL);
    if (err != AMG_OK) goto fail;

    /* Datasheet: wait 50 ms after reset before communicating */
    usleep(AMG_SETUP_TIME_MS * 1000);

    /* Normal operating mode */
    err = _reg_write(h, AMG_REG_PCTL, AMG_PCTL_NORMAL);
    if (err != AMG_OK) goto fail;

    /* Flag reset to clear any stale status bits */
    err = _reg_write(h, AMG_REG_RST, AMG_RST_FLAG);
    if (err != AMG_OK) goto fail;

    /* Disable interrupts (polling mode) */
    err = _reg_write(h, AMG_REG_INTC, AMG_INTC_DISABLED);
    if (err != AMG_OK) goto fail;

    /* Set frame rate */
    err = _reg_write(h, AMG_REG_FPSC,
                     (cfg->fps == AMG_FPS_1) ? AMG_FPSC_1FPS : AMG_FPSC_10FPS);
    if (err != AMG_OK) goto fail;

    /* Set hardware moving average */
    err = _reg_write(h, AMG_REG_AVE,
                     (cfg->hw_avg == AMG_AVG_TWICE) ? AMG_AVE_TWICE : AMG_AVE_NONE);
    if (err != AMG_OK) goto fail;

    fprintf(stderr, "[amg] initialised: %s addr=0x%02X fps=%s ema_alpha=%.2f\n",
            cfg->i2c_dev, cfg->i2c_addr,
            (cfg->fps == AMG_FPS_1) ? "1" : "10",
            cfg->ema_alpha);

    *out = h;
    return AMG_OK;

fail:
    close(h->fd);
    free(h);
    return err;
}

void amg_close(amg_handle_t *h)
{
    if (!h) return;
    if (h->fd >= 0) close(h->fd);
    free(h);
}

/* -----------------------------------------------------------------------
 * Public API — Data acquisition
 * ----------------------------------------------------------------------- */

amg_err_t amg_read_frame(amg_handle_t *h, amg_frame_t *frame)
{
    if (!h || !frame) return AMG_ERR_NULL;

    amg_err_t err;

    /* --- Stage 1: Check overflow status --- */
    uint8_t status = 0;
    err = _reg_read(h, AMG_REG_STAT, &status, 1);
    if (err != AMG_OK) return err;

    frame->overflow = (status & (AMG_STAT_OVF_FLG | AMG_STAT_OVF_IRS)) ? 1 : 0;

    if (frame->overflow) {
        /* Clear the overflow flags so next frame is clean */
        _reg_write(h, AMG_REG_SCLR, AMG_SCLR_CLR_ALL);
    }

    /* --- Stage 2: Read thermistor (regs 0x0E–0x0F) --- */
    uint8_t tb[2];
    err = _reg_read(h, AMG_REG_TTHL, tb, 2);
    if (err != AMG_OK) return err;

    /* 12-bit signed, bits [3:0] of MSB are sign/upper bits */
    uint16_t therm_raw = (uint16_t)tb[0] | ((uint16_t)(tb[1] & 0x0F) << 8);
    int16_t  therm_s   = _sign_extend12(therm_raw);
    frame->ambient_temp = (float)therm_s * AMG_THERM_RESOLUTION;

    /* --- Stage 3: Burst read pixel array (regs 0x80–0xFF, 128 bytes) --- */
    uint8_t raw[AMG_PIXEL_BYTES];
    err = _reg_read(h, AMG_REG_PIXEL_BASE, raw, AMG_PIXEL_BYTES);
    if (err != AMG_OK) return err;

    /* Record timestamp immediately after the I2C read */
    frame->timestamp_ms = _now_ms();

    float pixels[64];

    for (int i = 0; i < AMG_PIXEL_COUNT; i++) {
        /* Each pixel: 2 bytes, LSB first, 12-bit signed */
        uint16_t raw16 = (uint16_t)raw[i*2] | ((uint16_t)(raw[i*2+1] & 0x0F) << 8);
        int16_t  s16   = _sign_extend12(raw16);

        /* Stage 3a: Convert to °C (datasheet: 0.25 °C / LSB) */
        pixels[i] = (float)s16 * AMG_PIXEL_RESOLUTION;
    }

    /* --- Stage 4: Bad pixel clamping ---
     * Replace any pixel outside 0–80 °C (datasheet measurement range)
     * with the mean of its valid cardinal neighbours. */
    for (int i = 0; i < AMG_PIXEL_COUNT; i++) {
        if (pixels[i] < AMG_TEMP_MIN || pixels[i] > AMG_TEMP_MAX)
            _fix_bad_pixel(pixels, i, frame->ambient_temp);
    }

    /* --- Stage 5: EMA temporal filter ---
     * y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
     * alpha=0: freeze; alpha=1: no filtering.
     * Datasheet NETD at 10 Hz = 0.16 °C → filter is important. */
    if (h->ema_alpha > 0.0f && h->ema_alpha <= 1.0f) {
        if (!h->ema_init) {
            memcpy(h->ema, pixels, sizeof(pixels));
            h->ema_init = 1;
        } else {
            float a  = h->ema_alpha;
            float a1 = 1.0f - a;
            for (int i = 0; i < AMG_PIXEL_COUNT; i++)
                h->ema[i] = a * pixels[i] + a1 * h->ema[i];
        }
        memcpy(pixels, h->ema, sizeof(pixels));
    }

    /* --- Stage 6: Copy to frame + compute min/max --- */
    float mn = pixels[0], mx = pixels[0];
    for (int i = 0; i < AMG_PIXEL_COUNT; i++) {
        frame->pixels[i] = pixels[i];
        if (pixels[i] < mn) mn = pixels[i];
        if (pixels[i] > mx) mx = pixels[i];
    }
    frame->min_temp = mn;
    frame->max_temp = mx;

    return AMG_OK;
}

amg_err_t amg_read_thermistor(amg_handle_t *h, float *temp_c)
{
    if (!h || !temp_c) return AMG_ERR_NULL;

    uint8_t tb[2];
    amg_err_t err = _reg_read(h, AMG_REG_TTHL, tb, 2);
    if (err != AMG_OK) return err;

    uint16_t raw = (uint16_t)tb[0] | ((uint16_t)(tb[1] & 0x0F) << 8);
    *temp_c = (float)_sign_extend12(raw) * AMG_THERM_RESOLUTION;
    return AMG_OK;
}

/* -----------------------------------------------------------------------
 * Public API — Sensor control
 * ----------------------------------------------------------------------- */

amg_err_t amg_sleep(amg_handle_t *h)
{
    if (!h) return AMG_ERR_NULL;
    return _reg_write(h, AMG_REG_PCTL, AMG_PCTL_SLEEP);
}

amg_err_t amg_wake(amg_handle_t *h)
{
    if (!h) return AMG_ERR_NULL;
    amg_err_t err = _reg_write(h, AMG_REG_PCTL, AMG_PCTL_NORMAL);
    if (err != AMG_OK) return err;
    usleep(AMG_SETUP_TIME_MS * 1000);
    return AMG_OK;
}

amg_err_t amg_reset_flags(amg_handle_t *h)
{
    if (!h) return AMG_ERR_NULL;
    return _reg_write(h, AMG_REG_RST, AMG_RST_FLAG);
}

/* -----------------------------------------------------------------------
 * Utilities
 * ----------------------------------------------------------------------- */

const char *amg_strerror(amg_err_t err)
{
    switch (err) {
        case AMG_OK:           return "OK";
        case AMG_ERR_OPEN:     return "Failed to open I2C device";
        case AMG_ERR_IOCTL:    return "Failed to set I2C slave address";
        case AMG_ERR_READ:     return "I2C read error";
        case AMG_ERR_WRITE:    return "I2C write error";
        case AMG_ERR_OVERFLOW: return "Sensor overflow";
        case AMG_ERR_NULL:     return "NULL pointer";
        case AMG_ERR_TIMEOUT:  return "Timeout";
        default:               return "Unknown error";
    }
}
