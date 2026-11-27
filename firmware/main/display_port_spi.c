/* display_port_spi.c - THIS FORK's two 320 x 240 SPI boards (board_pins.h Q_*):
 *   - the 2.8" ESP32-S3 CYD (ES3C28P): ILI9341V   (CONFIG_POCKET_TANK_CYD28)
 *   - the Waveshare ESP32-S3-Touch-LCD-2: ST7789T3 (CONFIG_POCKET_TANK_TLCD2)
 * a 240 x 320 portrait panel scanned landscape through MADCTL (esp_lcd's
 * swap_xy / mirror), over 4-wire SPI.
 *
 * Since the 0.4.3 merge these boards run the rectangle's own 448 x 368 world:
 * nothing in common/ is laid out for 320 x 240 any more. This port shows the
 * frame scaled down to the glass - the exact area average of qvga_scale.h, 20
 * glass rows a stripe into an internal DMA buffer - and turns a touch on the
 * glass back into the frame (display_port_panel_to_tank; touch_port_ft3168.c
 * map_touch). That is the whole of the fork's display work at an upstream
 * release: a new page needs nothing here.
 *
 * The 180-degree flip is MADCTL with both mirrors toggled (quarter 2); the
 * boards do not stand on a side (display_port_quarters false). Brightness is
 * the backlight's PWM duty (LEDC). This port also owns the board's I2C bus
 * (touch, codec, IMU, the I2C socket) - board_i2c_bus(). */
#include "display_port.h"
#include "board_pins.h"
#include "qvga_scale.h"
#include "tank.h"
#include "render.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#if CONFIG_POCKET_TANK_TLCD2
#include "esp_lcd_panel_vendor.h"               /* esp_lcd_new_panel_st7789, part of esp_lcd itself */
#define new_panel esp_lcd_new_panel_st7789
static const char *TAG = "st7789";
#else
#include "esp_lcd_ili9341.h"
#define new_panel esp_lcd_new_panel_ili9341
static const char *TAG = "ili9341";
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

_Static_assert(TANK_W == QVGA_SRC_W && TANK_H == QVGA_SRC_H, "the QVGA boards scale the rectangle's 448 x 368 frame");

#define LCD_HOST    SPI2_HOST
#define STRIPE_ROWS 20                          /* glass rows per DMA transfer: 320 x 20 x 2 = 12.8 KB */

#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_stripe[2];                   /* QVGA_W x STRIPE_ROWS, DMA-capable; ping-pong */
static SemaphoreHandle_t s_stripe_free;         /* counts stripe buffers not in DMA flight */
static i2c_master_bus_handle_t s_i2c;
static uint8_t s_brightness = 0xFF;
static bool s_inverted, s_applied_inverted;     /* the flip wanted, and the one MADCTL has */
static bool s_up;
static qvga_scale_t *s_scale;
static int64_t s_send_us;

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io; (void)ev; (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_stripe_free, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void) {
    ledc_timer_config_t timer = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                                  .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = { .gpio_num = Q_PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                      .channel = BL_CHANNEL, .timer_sel = BL_TIMER, .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}
static void backlight_set(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

/* MADCTL for the current flip: upright uses the bench mirrors, flipped
 * inverts both, which is a 180-degree turn of the scan. */
static void apply_orientation(void) {
    esp_lcd_panel_swap_xy(s_panel, Q_LCD_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, Q_LCD_MIRROR_X ^ s_inverted, Q_LCD_MIRROR_Y ^ s_inverted);
    s_applied_inverted = s_inverted;
}

bool display_port_init(void) {
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = Q_PIN_I2C_SDA, .scl_io_num = Q_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    /* Who answers on the shared bus: the bring-up question for the codec and the IMU. */
    for (int address = 0x08; address < 0x78; address++)
        if (i2c_master_probe(s_i2c, address, 20) == ESP_OK) ESP_LOGI(TAG, "i2c: device at 0x%02X", address);

    backlight_init();                               /* dark until the first frame is in */
#ifdef Q_PIN_SD_CS
    /* the TF card shares the LCD's SPI lines: hold its select high so the
       card never answers a panel transfer */
    gpio_set_direction(Q_PIN_SD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(Q_PIN_SD_CS, 1);
#endif
    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(QVGA_W * STRIPE_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_stripe[i]) return false;
    }
    s_scale = heap_caps_calloc(1, sizeof *s_scale, MALLOC_CAP_INTERNAL);   /* 9 KB: the tables and three averaged lines, in fast RAM */
    if (!s_scale) return false;
    qvga_scale_init(s_scale);
    s_stripe_free = xSemaphoreCreateCounting(2, 2);

    const spi_bus_config_t spi = { .sclk_io_num = Q_PIN_LCD_SCLK, .mosi_io_num = Q_PIN_LCD_MOSI, .miso_io_num = Q_PIN_LCD_MISO,
                                   .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = QVGA_W * STRIPE_ROWS * 2 };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = { .cs_gpio_num = Q_PIN_LCD_CS, .dc_gpio_num = Q_PIN_LCD_DC,
        .spi_mode = 0, .pclk_hz = Q_LCD_PCLK_HZ, .trans_queue_depth = 4, .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    const esp_lcd_panel_dev_config_t pcfg = { .reset_gpio_num = Q_PIN_LCD_RST,
        .rgb_ele_order = Q_LCD_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16 };
    ESP_ERROR_CHECK(new_panel(io, &pcfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));  /* no reset pin (-1): the driver sends the software reset */
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    esp_lcd_panel_invert_color(s_panel, Q_LCD_INVERT);
    apply_orientation();
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    backlight_set(s_brightness);
    s_up = true;
    ESP_LOGI(TAG, "panel up: the %dx%d frame scaled to %dx%d landscape, %d MHz", TANK_W, TANK_H, QVGA_W, QVGA_H, Q_LCD_PCLK_HZ / 1000000);
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
void display_port_set_quarter(int q) { s_inverted = (q & 3) == 2; }   /* a side (1, 3) is ignored: these boards do not stand on one */
bool display_port_quarters(void) { return false; }

void display_port_set_brightness(uint8_t level) { s_brightness = level; if (s_up) backlight_set(level); }
uint8_t display_port_brightness(void) { return s_brightness; }

/* a touch on the upright glass (0..319, 0..239) -> the frame px under it */
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) { qvga_glass_to_frame(px, py, tx, ty); }

/* The landscape frame fb[y][x], TANK_W x TANK_H, scaled to the landscape scan:
 * each stripe is STRIPE_ROWS glass rows, written in the panel's byte order
 * (RGB565 is high byte first on the wire). Two buffers ping-pong, so scaling
 * stripe N+1 overlaps sending stripe N. */
void display_port_flush(const uint16_t *fb) {
    if (!s_up) return;
    int64_t t0 = esp_timer_get_time();
    /* A flip lands between frames, never mid-scan - but only once every
     * stripe of the frame before is out, because MADCTL is a command on the
     * same bus. */
    if (s_inverted != s_applied_inverted) {
        xSemaphoreTake(s_stripe_free, portMAX_DELAY); xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        apply_orientation();
        xSemaphoreGive(s_stripe_free); xSemaphoreGive(s_stripe_free);
    }
    qvga_scale_begin(s_scale);
    int current = 0;
    for (int y0 = 0; y0 < QVGA_H; y0 += STRIPE_ROWS) {
        int rows = QVGA_H - y0 < STRIPE_ROWS ? QVGA_H - y0 : STRIPE_ROWS;
        xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        uint16_t *stripe = s_stripe[current];
        qvga_scale_rows(s_scale, fb, PT_FB_SWAP, y0, rows, stripe, true);
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y0, QVGA_W, y0 + rows, stripe);
        if (err != ESP_OK) {
            xSemaphoreGive(s_stripe_free);          /* no transfer, so no completion will return it */
            static int logged;
            if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap y0=%d: %s", y0, esp_err_to_name(err));
        }
        current ^= 1;
    }
    s_send_us += esp_timer_get_time() - t0;
}
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) { *wait_us = 0; *send_us = s_send_us; s_send_us = 0; }
/* no flush task here: the frame goes out the plain way (main.c falls back on false) */
bool display_port_flush_async(const uint16_t *fb, void (*done)(const uint16_t *, void *), void *arg) { (void)fb; (void)done; (void)arg; return false; }
void display_port_flush_wait(const uint16_t *fb) { (void)fb; }
void display_port_flush_async_prof(int64_t *task_us, int *frames, int64_t *copy_us) { *task_us = 0; *frames = 0; *copy_us = 0; }

/* ---- the rest of the port's interface, as a board with none of the AMOLED family's parts ----
 * No round / watch / square glass, no IO expander, no PMIC rail to heal, no PWR-key sense line. The deep sleep is the
 * panel's display-off (display_port_sleep) and main.c's hold of every digital pad, as it was before the merge. */
bool board_is_v2(void) { return false; }
bool board_is_round(void) { return false; }
bool board_is_175(void) { return false; }
bool board_is_watch(void) { return false; }
bool board_is_square(void) { return false; }
bool board_has_expander(void) { return false; }
uint8_t board_es7210_addr(void) { return 0; }
const char *board_port_hw(void) { return NULL; }   /* no probe tells these boards apart: the image's own PT_BOARD stands (update.c) */
bool board_port_probed(void) { return true; }
int  board_pwr_sense_pin(void) { return -1; }
void board_sd_select(bool on) { (void)on; }
bool display_port_panel_power_check(void) { return false; }
uint32_t display_port_panel_heals(void) { return 0; }
int  display_port_boot_reg90(void) { return -1; }
bool display_port_panel_up(void) { return s_up; }
bool display_port_board_back(void) { return false; }
void display_port_panel_rail_test_off(void) {}
void display_port_side_tune(int madctl, int gx, int gy) { (void)madctl; (void)gx; (void)gy; }
void display_port_side_info(int *madctl, int *gx, int *gy, bool *on) { *madctl = 0; *gx = *gy = 0; *on = false; }
void display_port_probe(bool on) { (void)on; }
void display_port_set_madctl(uint8_t v) { (void)v; }
uint8_t display_port_madctl(void) { return 0; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FIT; }
void display_port_deep_sleep_pins(bool tp_awake_high) { (void)tp_awake_high; }
void display_port_deep_sleep_bus(void) {}
void display_port_deep_standby(void) {}
