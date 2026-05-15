#ifndef AMG8833_REGS_H
#define AMG8833_REGS_H

/* -----------------------------------------------------------------------
 * AMG8833 / Grid-EYE Register Map
 * Panasonic Grid-EYE Datasheet (AMG88 series), Apr. 2017
 * ----------------------------------------------------------------------- */

/* I2C slave addresses — set by AD_SELECT pin */
#define AMG_ADDR_LOW   0x68   /* AD_SELECT → GND */
#define AMG_ADDR_HIGH  0x69   /* AD_SELECT → VDD */

/* Power control register (0x00) */
#define AMG_REG_PCTL        0x00
#define AMG_PCTL_NORMAL     0x00   /* Normal operating mode */
#define AMG_PCTL_SLEEP      0x10   /* Sleep mode            */
#define AMG_PCTL_STANDBY60  0x20   /* Stand-by 60 s         */
#define AMG_PCTL_STANDBY10  0x21   /* Stand-by 10 s         */

/* Software reset register (0x01) */
#define AMG_REG_RST         0x01
#define AMG_RST_FLAG        0x30   /* Flag reset   */
#define AMG_RST_INITIAL     0x3F   /* Initial reset */

/* Frame rate register (0x02) */
#define AMG_REG_FPSC        0x02
#define AMG_FPSC_10FPS      0x00   /* 10 frames/sec (default) */
#define AMG_FPSC_1FPS       0x01   /*  1 frame/sec            */

/* Interrupt control register (0x03) */
#define AMG_REG_INTC        0x03
#define AMG_INTC_DISABLED   0x00
#define AMG_INTC_ABS_MODE   0x03   /* Absolute value mode */
#define AMG_INTC_DIFF_MODE  0x01   /* Difference mode     */

/* Status register (0x04) */
#define AMG_REG_STAT        0x04
#define AMG_STAT_OVF_IRS    (1 << 2)  /* Thermistor overflow */
#define AMG_STAT_OVF_FLG    (1 << 1)  /* Temperature overflow */
#define AMG_STAT_INTF       (1 << 0)  /* Interrupt flag       */

/* Status clear register (0x05) */
#define AMG_REG_SCLR        0x05
#define AMG_SCLR_CLR_ALL    0x07

/* Moving average register (0x07) */
#define AMG_REG_AVE         0x07
#define AMG_AVE_TWICE       0x20   /* Twice moving average */
#define AMG_AVE_NONE        0x00   /* No moving average    */

/* Interrupt upper/lower limit registers (0x08–0x0D) */
#define AMG_REG_INTHL       0x08   /* Interrupt upper limit LSB */
#define AMG_REG_INTHH       0x09   /* Interrupt upper limit MSB */
#define AMG_REG_INTLL       0x0A   /* Interrupt lower limit LSB */
#define AMG_REG_INTLH       0x0B   /* Interrupt lower limit MSB */
#define AMG_REG_IHYSL       0x0C   /* Interrupt hysteresis LSB  */
#define AMG_REG_IHYSH       0x0D   /* Interrupt hysteresis MSB  */

/* Thermistor output registers (0x0E–0x0F) — 12-bit signed, 0.0625 °C/LSB */
#define AMG_REG_TTHL        0x0E   /* Thermistor LSB */
#define AMG_REG_TTHH        0x0F   /* Thermistor MSB */

/* Pixel output registers: 0x80–0xFF
 * 64 pixels × 2 bytes each = 128 bytes
 * 12-bit signed two's complement, LSB first, 0.25 °C/LSB */
#define AMG_REG_PIXEL_BASE  0x80
#define AMG_PIXEL_COUNT     64
#define AMG_PIXEL_BYTES     128

/* Sensor physical constants (from datasheet) */
#define AMG_PIXEL_RESOLUTION    0.25f    /* °C per count  */
#define AMG_THERM_RESOLUTION    0.0625f  /* °C per count  */
#define AMG_TEMP_MIN            0.0f     /* High-gain measurement min */
#define AMG_TEMP_MAX            80.0f    /* High-gain measurement max */
#define AMG_ACCURACY_TYPICAL    2.5f     /* ±°C typical accuracy      */
#define AMG_NETD_10HZ           0.16f    /* NETD at 10 Hz (°C)        */
#define AMG_SETUP_TIME_MS       50       /* ms after init to enable comms */
#define AMG_STABILISE_TIME_MS   15000   /* ms to stabilise output after init */

#endif /* AMG8833_REGS_H */
