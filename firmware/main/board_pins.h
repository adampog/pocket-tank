/* board_pins.h — the boards one image runs on, told apart at boot over I2C
 * (display_port_init; SDA / SCL, the QSPI data lines and LCD_CS are shared):
 *
 * Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1: SH8601 + FT3168; V2: CO5300 +
 * CST816). Sources: Waveshare esp-idf examples and the official Arduino
 * variant. VERIFY I2C SDA/SCL on the bench: Waveshare's own code says
 * SDA=15/SCL=14, the Arduino variant says the reverse.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-1.75C (2026-10-01; ROUND 466x466 CO5300 +
 * CST9217). Sources: resources/ESP32-S3-Touch-AMOLED-1.75C/ (the schematic's
 * GPIO table) and Waveshare's BSP (waveshare/esp32_s3_touch_amoled_1_75c).
 * No IO expander - the resets are GPIOs - no RTC chip, no SD card; an ES7210
 * microphone ADC the 1.8 does not have (its I2C address is how the board is
 * recognized). PMIC, IMU, codec, amp and every audio pin: as on the 1.8.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-2.06, the WATCH (2026-10-02; 410x502
 * CO5300 + FT3168, the 1.8's panel family a size up; worn, so its own build
 * is a PORTRAIT 410x502 tank, the panel unturned). Sources: resources/ESP32-S3-Touch-AMOLED-2.06-Watch/ (schematic,
 * wiki page) and Waveshare's BSP (waveshare/esp32_s3_touch_amoled_2_06).
 * No IO expander: the resets are GPIOs 8 and 9 - the pins the 1.8 plays its
 * I2S bit clock and data on, so the AUDIO PINS differ here (audio_port) and
 * the 1.8's would hold the panel in reset. An ES7210 like the 1.75C's, and an
 * RTC chip unlike it: no expander + ES7210 + RTC = the watch. DSI_PWR_EN is
 * not a GPIO: it is pulled up to ALDO2, so that rail IS the panel's power
 * switch. PWR key sense on GPIO 10 (SYS_OUT), an SD slot (1/2/3/17) and a
 * vibration motor (GPIO 18, fed from ALDO3) the tank does not use. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "sdkconfig.h"

#if CONFIG_POCKET_TANK_BOARD_CYD28
/* The 2.8" ESP32-S3 CYD, ES3C28P (lcdwiki.com, ES3C28P_ES2N28P_Specification_V1.0
 * section 4.2). A stock board, no modifications: everything below is as the
 * vendor wires it. The LCD's reset is CHIP_PU - it resets with the chip, so
 * there is no reset pin to drive. */
#define PIN_LCD_CS        10
#define PIN_LCD_DC        46       /* high = data, low = command */
#define PIN_LCD_SCLK      12
#define PIN_LCD_MOSI      11
#define PIN_LCD_MISO      13
#define PIN_LCD_BL        45       /* high = backlight on; PWM for brightness */
#define PIN_LCD_RST       -1       /* CHIP_PU */
/* How the glass is mounted, found on the bench (display_port_spi.c). swap_xy
 * turns the portrait panel landscape; the two mirrors pick which corner is
 * the origin. The colour order and inversion are the two other things CYD
 * clones differ in. 40 MHz is inside what every ILI9341 clone takes for
 * writes; 80 is worth a try once the picture is right (a frame is 30 ms at 40). */
#define LCD_PCLK_HZ       (40 * 1000 * 1000)
#define LCD_SWAP_XY       true
#define LCD_MIRROR_X      false
#define LCD_MIRROR_Y      false
#define LCD_BGR           true
#define LCD_INVERT        true     /* IPS ILI9341V panels want inversion on */
#define PIN_I2C_SDA       16       /* shared: touch, audio codec, the I2C socket */
#define PIN_I2C_SCL       15
#define PIN_TP_RST        18       /* low = reset */
#define PIN_TP_INT        17       /* low while touched (the port polls instead) */
#define I2C_ADDR_FT6336   0x38
#define PANEL_W           240      /* native portrait; the panel scans landscape (MADCTL) */
#define PANEL_H           320
/* audio: the ES8311 (I2C 0x18) on I2S, and the power amplifier's enable */
#define PIN_I2S_MCLK      4
#define PIN_I2S_BCLK      5
#define PIN_I2S_WS        7
#define PIN_I2S_DOUT      8        /* ESP -> codec DSDIN; GPIO6 is the microphone's way back, unused */
#define PIN_AMP_EN        1
#define AMP_EN_ON         0        /* the spec: "low level enable" */
#elif CONFIG_POCKET_TANK_BOARD_TLCD2
/* The Waveshare ESP32-S3-Touch-LCD-2 (waveshare.com/wiki/ESP32-S3-Touch-LCD-2:
 * the ESP-IDF demos' main.c, the Arduino factory app, and the schematic's
 * netlist). A stock board, no modifications. The LCD's and the touch panel's
 * resets are one net with a pull-up (and an unfitted link to GPIO0): no reset
 * pin to drive, the driver sends the software reset. No codec, no PMIC, no
 * RTC chip; the charger's status only lights an LED. */
#define PIN_LCD_CS        45       /* a strapping pin: no pull-up on it, ever */
#define PIN_LCD_DC        42       /* high = data, low = command */
#define PIN_LCD_SCLK      39       /* shared with the TF card */
#define PIN_LCD_MOSI      38       /* shared with the TF card */
#define PIN_LCD_MISO      40       /* the TF card's only: the panel is write-only */
#define PIN_LCD_BL        1        /* high = backlight on (an NPN low-side switch); PWM for brightness */
#define PIN_LCD_RST       -1
#define PIN_SD_CS         41       /* held high: the card stays off the panel's bus */
/* The factory app's landscape: rotation 1 = MADCTL MX | MV, RGB order, IPS
 * inversion on; Waveshare's demos clock the panel at 80 MHz. */
#define LCD_PCLK_HZ       (80 * 1000 * 1000)
#define LCD_SWAP_XY       true
#define LCD_MIRROR_X      true
#define LCD_MIRROR_Y      false
#define LCD_BGR           false
#define LCD_INVERT        true
#define PIN_I2C_SDA       48       /* shared: touch, IMU, the P2 header */
#define PIN_I2C_SCL       47
#define PIN_TP_RST        -1       /* the LCD's reset net (above) */
#define PIN_TP_INT        46       /* low while touched (the port polls instead); a strapping pin */
#define I2C_ADDR_CST816D  0x15
#define PIN_IMU_INT1      3        /* QMI8658 at 0x6B, unused (the port polls) */
#define PIN_BAT_ADC       5        /* ADC1_CH4: VBAT through 200K / 100K, so x3 */
#define PANEL_W           240      /* native portrait; the panel scans landscape (MADCTL) */
#define PANEL_H           320
#elif CONFIG_POCKET_TANK_BOARD_CORES3
/* The M5Stack CoreS3 (M5GFX 0.2.32 / M5Unified 0.2.25, in
 * ~/Projects/firmware/.pio/libdeps/m5stack-cores3/, and the mission brief). An
 * ILI9342C 320x240 IPS over SPI - register-compatible with the ILI9341 init,
 * so it rides display_port_spi.c. D/C and MISO SHARE GPIO35; the panel is used
 * write-only, so there is no MISO. The backlight is AXP2101 DLDO1 (not a PWM
 * GPIO) and the LCD reset is AW9523B P1_1 - both driven by the PMIC/expander
 * bring-up (slice 02), so there is no reset or backlight GPIO here. Internal
 * I2C (touch, PMIC, expander) is SDA GPIO12 / SCL GPIO11. */
#define PIN_LCD_CS        3
#define PIN_LCD_DC        35       /* high = data, low = command (shares GPIO35 with the unused MISO) */
#define PIN_LCD_SCLK      36
#define PIN_LCD_MOSI      37
#define PIN_LCD_MISO      -1       /* write-only: the panel never reads back (D/C owns GPIO35) */
#define PIN_LCD_BL        -1       /* AXP2101 DLDO1, not a GPIO - driven in the bring-up slice */
#define PIN_LCD_RST       -1       /* AW9523B P1_1, not a GPIO - driven in the bring-up slice */
#define PIN_SD_CS         4        /* TF card CS: held high so the card stays off the panel's bus */
/* PROVISIONAL landscape (ILI9341-family defaults; M5GFX drives the real
 * ILI9342C rotation=1). The exact MADCTL / colour order are verified on the
 * bench in the bring-up slice - this slice only has to configure and build. */
#define LCD_PCLK_HZ       (40 * 1000 * 1000)
#define LCD_SWAP_XY       true
#define LCD_MIRROR_X      false
#define LCD_MIRROR_Y      false
#define LCD_BGR           true
#define LCD_INVERT        true     /* the IPS panel wants inversion on (brief) */
#define PIN_I2C_SDA       12       /* internal bus: touch (FT6336), AXP2101, AW9523B */
#define PIN_I2C_SCL       11
#define PIN_TP_RST        -1       /* touch reset is via the AW9523B (bring-up slice) */
#define PIN_TP_INT        -1       /* INT routes through the AW9523B to GPIO21; the port polls */
#define I2C_ADDR_FT6336   0x38
#define I2C_ADDR_AXP2101  0x34     /* PMIC: LDOs + DLDO1 backlight (bring-up slice) */
#define I2C_ADDR_AW9523   0x58     /* IO expander: LCD reset, boost enable, touch INT (bring-up slice) */
#define PANEL_W           240      /* native portrait; the panel scans landscape (MADCTL) */
#define PANEL_H           320
#else
#define PIN_LCD_CS        12
#define PIN_LCD_PCLK      11
#define PIN_LCD_DATA0     4
#define PIN_LCD_DATA1     5
#define PIN_LCD_DATA2     6
#define PIN_LCD_DATA3     7
#define PIN_I2C_SDA       15
#define PIN_I2C_SCL       14
#define PIN_TP_INT        21
#define I2C_ADDR_EXPANDER 0x20     /* TCA9554: bit0 LCD_RST, bit1 DSI_PWR_EN, bit2 TOUCH_RST, bit7 SD_CS */
#define I2C_ADDR_FT3168   0x38     /* V1 touch */
#define I2C_ADDR_CST816   0x15     /* V2 touch (probe => V2 board) */
#define PANEL_W           368      /* native portrait */
#define PANEL_H           448
#define V2_PANEL_X_GAP    0x10
/* audio (resources/ESP32-S3-Touch-AMOLED-1.8.pdf): the ES8311 on I2S, the NS4150B's CTRL */
#define PIN_I2S_MCLK      16
#define PIN_I2S_BCLK      9
#define PIN_I2S_WS        45
#define PIN_I2S_DOUT      8        /* ESP -> codec DSDIN */
#define PIN_AMP_EN        46       /* NS4150B CTRL, 10k pulldown on the board */
#define AMP_EN_ON         1

/* The round 1.75C and the 2.06 watch are AMOLED-family (QSPI) variants of this
 * #else branch, told apart at boot by board_is_round()/board_is_watch(); their
 * pin/panel overrides are globals the display/touch/audio ports pick at runtime. */
/* ---- the 1.75C ---- */
#define R_PIN_LCD_PCLK    38
#define R_PIN_LCD_RST     1
#define R_PIN_LCD_TE      13       /* unused */
#define R_PIN_TP_RST      2
#define R_PIN_TP_INT      11
#define R_PIN_IMU_INT1    21       /* unused */
#define R_PIN_PWR_SENSE   3        /* SYS_OUT: high while the PWR key is down (unused: the PMIC is asked, as on the 1.8) */
#define I2C_ADDR_ES7210   0x40     /* the microphone ADC (probe, with no expander => the 1.75C) */
#define I2C_ADDR_CST9217  0x5A
#define R_PANEL           466      /* round: 466 across, the corners of the square are not there */
#define R_PANEL_X_GAP     6

/* ---- the 2.06 watch ---- */
#define W_PIN_LCD_RST     8
#define W_PIN_TP_RST      9
#define W_PIN_TP_INT      38       /* unused: polled, like the others */
#define W_PIN_LCD_TE      13       /* unused */
#define W_PIN_PWR_SENSE   10       /* SYS_OUT: high while the PWR key is down */
#define W_PIN_I2S_BCLK    41
#define W_PIN_I2S_DOUT    40       /* ESP -> codec DSDIN */
#define W_PIN_MOTOR       18       /* unused (pulled off on the board) */
#define I2C_ADDR_RTC      0x51     /* PCF85063: the 1.8 and the watch have one, the 1.75C does not */
#define W_PANEL_W         410      /* native portrait */
#define W_PANEL_H         502
#define W_PANEL_X_GAP     0x16
#endif  /* board */
#endif
