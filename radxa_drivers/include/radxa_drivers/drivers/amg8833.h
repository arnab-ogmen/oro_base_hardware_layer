#ifndef AMG8833_H
#define AMG8833_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * AMG8833 Driver — Public API
 * Platform: Radxa CM5, /dev/i2c-7, Debian 11
 * ----------------------------------------------------------------------- */

/* Forward declaration */
typedef struct amg_handle amg_handle_t;

/* Error codes */
typedef enum {
    AMG_OK             =  0,
    AMG_ERR_OPEN       = -1,   /* Failed to open I2C device */
    AMG_ERR_IOCTL      = -2,   /* Failed to set I2C slave address */
    AMG_ERR_READ       = -3,   /* I2C read error */
    AMG_ERR_WRITE      = -4,   /* I2C write error */
    AMG_ERR_OVERFLOW   = -5,   /* Sensor reported overflow */
    AMG_ERR_NULL       = -6,   /* NULL pointer argument */
    AMG_ERR_TIMEOUT    = -7,   /* Operation timed out */
} amg_err_t;

/* Frame rate options */
typedef enum {
    AMG_FPS_10 = 0,   /* 10 frames/sec — default, lower NETD */
    AMG_FPS_1  = 1,   /*  1 frame/sec  — lowest power        */
} amg_fps_t;

/* On-chip moving average */
typedef enum {
    AMG_AVG_NONE  = 0,   /* No hardware moving average */
    AMG_AVG_TWICE = 1,   /* Twice moving average (on-chip) */
} amg_avg_t;

/* Configuration passed to amg_init() */
typedef struct {
    const char  *i2c_dev;    /* e.g. "/dev/i2c-7" */
    uint8_t      i2c_addr;   /* AMG_ADDR_LOW (0x68) or AMG_ADDR_HIGH (0x69) */
    amg_fps_t    fps;        /* Frame rate */
    amg_avg_t    hw_avg;     /* Hardware moving average */
    float        ema_alpha;  /* Software EMA: 0.0 = off, 0.1–0.5 typical */
} amg_config_t;

/* A single processed thermal frame */
typedef struct __attribute__((packed)) {
    uint32_t  timestamp_ms;     /* Monotonic timestamp, milliseconds   */
    float     ambient_temp;     /* Thermistor (sensor die) °C          */
    float     pixels[64];       /* 8×8 array, °C, row-major [row*8+col] */
    float     min_temp;         /* Minimum pixel value this frame      */
    float     max_temp;         /* Maximum pixel value this frame      */
    uint8_t   overflow;         /* 1 if sensor overflow flag was set   */
    uint8_t   _pad[3];
} amg_frame_t;

/* -----------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

/**
 * Initialise the driver and sensor.
 * Allocates and returns a handle. Caller must free with amg_close().
 * Blocks for AMG_SETUP_TIME_MS (50 ms) after reset.
 *
 * @param cfg   Pointer to filled amg_config_t
 * @param out   Set to newly allocated handle on success
 * @return      AMG_OK or negative error code
 */
amg_err_t amg_init(const amg_config_t *cfg, amg_handle_t **out);

/**
 * Close the driver and release all resources.
 * Safe to call with NULL.
 */
void amg_close(amg_handle_t *h);

/* -----------------------------------------------------------------------
 * Data acquisition
 * ----------------------------------------------------------------------- */

/**
 * Read one complete frame from the sensor.
 * Performs: burst I2C read → sign-extend → convert → compensate →
 *           clamp → EMA filter → min/max → write to *frame.
 *
 * @param h      Driver handle
 * @param frame  Output frame (caller-allocated)
 * @return       AMG_OK or negative error code
 */
amg_err_t amg_read_frame(amg_handle_t *h, amg_frame_t *frame);

/**
 * Read thermistor temperature only (no pixel read).
 * Useful for ambient monitoring without full frame cost.
 */
amg_err_t amg_read_thermistor(amg_handle_t *h, float *temp_c);

/* -----------------------------------------------------------------------
 * Sensor control
 * ----------------------------------------------------------------------- */

/** Enter sleep mode (saves ~4.3 mA) */
amg_err_t amg_sleep(amg_handle_t *h);

/** Wake from sleep and return to normal mode */
amg_err_t amg_wake(amg_handle_t *h);

/** Perform software reset (flag reset — preserves config) */
amg_err_t amg_reset_flags(amg_handle_t *h);

/* -----------------------------------------------------------------------
 * Utilities
 * ----------------------------------------------------------------------- */

/** Return human-readable string for an error code */
const char *amg_strerror(amg_err_t err);

/** Return pixel index for row r (0–7), column c (0–7) */
static inline int amg_pixel_idx(int r, int c) { return r * 8 + c; }

#ifdef __cplusplus
}
#endif

#endif /* AMG8833_H */
