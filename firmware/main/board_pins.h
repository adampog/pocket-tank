/* board_pins.h — Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1: SH8601 + FT3168;
 * V2: CO5300 + CST816). Sources: Waveshare esp-idf examples and the official
 * Arduino variant. VERIFY I2C SDA/SCL on the bench: Waveshare's own code says
 * SDA=15/SCL=14, the Arduino variant says the reverse. */
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
#endif  /* board */
#endif
