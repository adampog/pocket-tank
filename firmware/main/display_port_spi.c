/* display_port_spi.c — a 240x320 portrait SPI LCD scanned as landscape 320x240,
 * via esp_lcd over 4-wire SPI:
 *   - the 2.8" ESP32-S3 CYD (ES3C28P): ILI9341V (CONFIG_POCKET_TANK_DISPLAY_ILI9341)
 *   - the Waveshare ESP32-S3-Touch-LCD-2: ST7789T3 (CONFIG_POCKET_TANK_DISPLAY_ST7789)
 * The pins and the bench facts of how each glass is mounted (LCD_SWAP_XY,
 * LCD_MIRROR_*, LCD_BGR, LCD_INVERT, LCD_PCLK_HZ) live in board_pins.h.
 *
 * Unlike the AMOLED port there is no software transpose: the controller turns
 * its own scan through MADCTL (esp_lcd's swap_xy / mirror), so a landscape
 * frame goes out row by row, only byte-swapped (RGB565 is big-endian on the
 * wire). The 180-degree flip is the same register with both mirrors toggled.
 * Brightness is the backlight's PWM duty (LEDC); neither panel has a
 * brightness command worth using.
 *
 * This port also owns the board's I2C bus (touch, codec, IMU, the I2C socket),
 * as the AMOLED port does - board_i2c_bus() is how everything else reaches it. */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#if CONFIG_POCKET_TANK_DISPLAY_ST7789
#include "esp_lcd_panel_vendor.h"               /* esp_lcd_new_panel_st7789, part of esp_lcd itself */
#define new_panel esp_lcd_new_panel_st7789
static const char *TAG = "st7789";
#else
#include "esp_lcd_ili9341.h"
#define new_panel esp_lcd_new_panel_ili9341
static const char *TAG = "ili9341";
#endif
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define LCD_HOST    SPI2_HOST
#define STRIPE_ROWS 20                          /* tank rows per DMA transfer: 320 x 20 x 2 = 12.8 KB */

#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_stripe[2];                   /* TANK_W x STRIPE_ROWS, DMA-capable; ping-pong */
static SemaphoreHandle_t s_stripe_free;         /* counts stripe buffers not in DMA flight */
static i2c_master_bus_handle_t s_i2c;
static uint8_t s_brightness = 0xFF;
static bool s_inverted, s_applied_inverted;     /* the flip wanted, and the one MADCTL has */

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_v2(void) { return false; }        /* the AMOLED's revision question; never asked here */

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io; (void)ev; (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_stripe_free, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void) {
#if PIN_LCD_BL < 0
    /* No PWM backlight GPIO (CoreS3: the backlight is AXP2101 DLDO1, driven in
     * slice 03). Configuring an LEDC channel on GPIO -1 aborts, so skip it. */
#else
    ledc_timer_config_t timer = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                                  .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                      .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
#endif
}

static void backlight_set(uint8_t level) {
#if PIN_LCD_BL < 0
    (void)level;   /* DLDO1 backlight is slice 03 */
#else
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
#endif
}

/* MADCTL for the current flip: upright uses the bench mirrors, flipped
 * inverts both, which is a 180-degree turn of the scan. */
static void apply_orientation(void) {
    esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X ^ s_inverted, LCD_MIRROR_Y ^ s_inverted);
    s_applied_inverted = s_inverted;
}

#if CONFIG_POCKET_TANK_BOARD_CORES3
/* CoreS3 pre-panel bring-up. The ILI9342C has no power or reset GPIO on this
 * board (PIN_LCD_RST/PIN_LCD_BL are -1): its VCI rail hangs off the AXP2101 PMIC
 * (0x34) and its reset line off the AW9523B IO-expander (0x58), both on the
 * internal I2C bus. M5GFX powers these before it ever touches the panel, so we
 * do the same here, before new_panel / esp_lcd_panel_reset below.
 *
 * The register writes mirror the vendor sequence exactly (see the slice proof
 * note): AW9523B from M5GFX 0.2.32 (M5GFX.cpp board-detect, ~L1769-1781) and the
 * AXP2101 ALDO rails from M5Unified 0.2.25 (Power_Class.inl CoreS3 case,
 * ~L409-430). AW9523B P1_7 (0x03 bit7) enables the SY7088 boost; P1_1 (0x03
 * bit1) driven high releases the LCD from reset (power-on default low = held in
 * reset). AXP2101 ALDO2 (0x93, 3.3 V) is the panel's VCI. The DLDO1 backlight is
 * slice 03, not here.
 *
 * Best-effort, like the AMOLED expander_power_up() and the battery port: in QEMU
 * (or with no chip present) nothing ACKs, so each step logs and continues rather
 * than hang the boot - the panel init that follows then fails, which is expected
 * off-target. The read-modify-write helpers match M5GFX's i2c::bitOn/bitOff. */
static bool cores3_reg_wr(i2c_master_dev_handle_t d, uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(d, b, 2, 100) == ESP_OK;
}
static bool cores3_reg_biton(i2c_master_dev_handle_t d, uint8_t reg, uint8_t mask) {
    uint8_t v, r = reg;
    return i2c_master_transmit_receive(d, &r, 1, &v, 1, 100) == ESP_OK
        && cores3_reg_wr(d, reg, (uint8_t)(v | mask));
}
static bool cores3_reg_bitoff(i2c_master_dev_handle_t d, uint8_t reg, uint8_t mask) {
    uint8_t v, r = reg;
    return i2c_master_transmit_receive(d, &r, 1, &v, 1, 100) == ESP_OK
        && cores3_reg_wr(d, reg, (uint8_t)(v & ~mask));
}
static void cores3_power_up(void) {
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t dev;
    int ok = 0, n = 0;

    /* AW9523B (0x58): output levels, direction, GPIO (not LED) mode; this is also
     * where SY7088 boost (P1_7) comes on and LCD reset (P1_1) is released. */
    cfg.device_address = I2C_ADDR_AW9523;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) == ESP_OK) {
        n += 7;
        ok += cores3_reg_biton(dev, 0x02, 0b00000111);   /* P0 output levels */
        ok += cores3_reg_biton(dev, 0x03, 0b10000011);   /* P1 levels: bit7 SY7088 boost EN, bit1 LCD reset release */
        ok += cores3_reg_wr   (dev, 0x04, 0b00011000);   /* P0 direction (CONFIG_P0) */
        ok += cores3_reg_wr   (dev, 0x05, 0b00001100);   /* P1 direction (CONFIG_P1) */
        ok += cores3_reg_wr   (dev, 0x11, 0b00010000);   /* GCR: P0 push-pull */
        ok += cores3_reg_wr   (dev, 0x12, 0xFF);         /* LEDMODE_P0 -> GPIO mode */
        ok += cores3_reg_wr   (dev, 0x13, 0xFF);         /* LEDMODE_P1 -> GPIO mode */
        i2c_master_bus_rm_device(dev);
    } else ESP_LOGW(TAG, "CoreS3: AW9523B (0x%02X) absent - boost/reset not driven (expected in QEMU)", I2C_ADDR_AW9523);

    /* AXP2101 (0x34): disable the DCDC UVP power-off latch, LDOs on, ALDO rails. */
    cfg.device_address = I2C_ADDR_AXP2101;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &dev) == ESP_OK) {
        n += 6;
        ok += cores3_reg_bitoff(dev, 0x23, 0x1F);        /* DCDC1-5 UVP power-off disable */
        ok += cores3_reg_wr    (dev, 0x90, 0xBF);        /* LDOs on (ALDO1-4, BLDO1-2, DLDO1) */
        ok += cores3_reg_wr    (dev, 0x92, 18 - 5);      /* ALDO1 = 1.8 V */
        ok += cores3_reg_wr    (dev, 0x93, 33 - 5);      /* ALDO2 = 3.3 V (panel VCI) */
        ok += cores3_reg_wr    (dev, 0x94, 33 - 5);      /* ALDO3 = 3.3 V */
        ok += cores3_reg_wr    (dev, 0x95, 33 - 5);      /* ALDO4 = 3.3 V */
        i2c_master_bus_rm_device(dev);
    } else ESP_LOGW(TAG, "CoreS3: AXP2101 (0x%02X) absent - panel rails not powered (expected in QEMU)", I2C_ADDR_AXP2101);

    if (ok == n) ESP_LOGI(TAG, "CoreS3 pre-panel bring-up: AW9523B + AXP2101 rails up (%d/%d writes acked)", ok, n);
    else ESP_LOGW(TAG, "CoreS3 pre-panel bring-up: %d/%d I2C writes acked (chip absent or no-ACK); continuing", ok, n);

    vTaskDelay(pdMS_TO_TICKS(120));   /* let the boosted rail + ALDO2 settle and the panel leave reset before init */
}
#endif /* CONFIG_POCKET_TANK_BOARD_CORES3 */

bool display_port_init(void) {
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
#if CONFIG_POCKET_TANK_BOARD_CORES3
    /* Power the panel rail and release its reset (AXP2101 + AW9523B) before any
     * panel traffic - the ILI9342C has no power/reset GPIO on the CoreS3. */
    cores3_power_up();
#endif
    /* Who answers on the shared bus: the bring-up question for the codec. */
    for (int address = 0x08; address < 0x78; address++)
        if (i2c_master_probe(s_i2c, address, 20) == ESP_OK) ESP_LOGI(TAG, "i2c: device at 0x%02X", address);

    backlight_init();                               /* dark until the first frame is in */
#ifdef PIN_SD_CS
    /* the TF card shares the LCD's SPI lines: hold its select high so the
       card never answers a panel transfer */
    gpio_set_direction(PIN_SD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_SD_CS, 1);
#endif

    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(TANK_W * STRIPE_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_stripe[i]) return false;
    }
    s_stripe_free = xSemaphoreCreateCounting(2, 2);

    const spi_bus_config_t spi = { .sclk_io_num = PIN_LCD_SCLK, .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = PIN_LCD_MISO,
                                   .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = TANK_W * STRIPE_ROWS * 2 };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = { .cs_gpio_num = PIN_LCD_CS, .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0, .pclk_hz = LCD_PCLK_HZ, .trans_queue_depth = 4, .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    const esp_lcd_panel_dev_config_t pcfg = { .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16 };
    ESP_ERROR_CHECK(new_panel(io, &pcfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));  /* no reset pin (-1): the driver sends the software reset */
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    esp_lcd_panel_invert_color(s_panel, LCD_INVERT);
    apply_orientation();
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    backlight_set(s_brightness);
    ESP_LOGI(TAG, "panel up: %dx%d landscape at %d MHz", TANK_W, TANK_H, LCD_PCLK_HZ / 1000000);
    return true;
}

void display_port_sleep(void) {
    backlight_set(0);
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
}

void display_port_wake(void) {
    if (!s_panel) return;
    esp_lcd_panel_disp_on_off(s_panel, true);
    backlight_set(s_brightness);
}

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }

void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    backlight_set(level);
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* Landscape fb[y][x], TANK_W x TANK_H, straight to the landscape scan: each
 * stripe is STRIPE_ROWS whole rows, byte-swapped into an internal DMA buffer.
 * Two buffers ping-pong, so filling stripe N+1 overlaps sending stripe N. */
void display_port_flush(const uint16_t *fb) {
    /* A flip lands between frames, never mid-scan - but only once every
     * stripe of the frame before is out, because MADCTL is a command on the
     * same bus. */
    if (s_inverted != s_applied_inverted) {
        xSemaphoreTake(s_stripe_free, portMAX_DELAY); xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        apply_orientation();
        xSemaphoreGive(s_stripe_free); xSemaphoreGive(s_stripe_free);
    }
    int current = 0;
    for (int y0 = 0; y0 < TANK_H; y0 += STRIPE_ROWS) {
        int rows = TANK_H - y0 < STRIPE_ROWS ? TANK_H - y0 : STRIPE_ROWS;
        xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        uint16_t *stripe = s_stripe[current];
        const uint32_t *source = (const uint32_t *)(fb + y0 * TANK_W);
        uint32_t *destination = (uint32_t *)stripe;
        /* Two pixels per 32-bit word: swap the bytes inside each half. */
        for (int i = 0; i < rows * TANK_W / 2; i++) {
            uint32_t pair = source[i];
            destination[i] = ((pair & 0x00FF00FFu) << 8) | ((pair & 0xFF00FF00u) >> 8);
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y0, TANK_W, y0 + rows, stripe);
        if (err != ESP_OK) {
            xSemaphoreGive(s_stripe_free);          /* no transfer, so no completion will return it */
            static int logged;
            if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap y0=%d: %s", y0, esp_err_to_name(err));
        }
        current ^= 1;
    }
}
