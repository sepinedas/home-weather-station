/*
 * epd213.c - SSD1680 driver for the CrowPanel ESP32-S3 2.13" e-paper,
 *            ported to ESP-IDF from Elecrow's official Arduino demo
 *            (register sequence kept identical).
 */
#include "epd213.h"

#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

/* ------------------------------------------------------------------ */
/* Low level: GPIO + bit-banged SPI                                    */
/* ------------------------------------------------------------------ */

static inline void pin(int io, int level) { gpio_set_level(io, level); }

static void epd_gpio_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << EPD_PIN_SCK)  | (1ULL << EPD_PIN_MOSI) |
                        (1ULL << EPD_PIN_RES)  | (1ULL << EPD_PIN_DC)   |
                        (1ULL << EPD_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);

    gpio_config_t in = {
        .pin_bit_mask = 1ULL << EPD_PIN_BUSY,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in);

    pin(EPD_PIN_CS, 1);
    pin(EPD_PIN_SCK, 0);
}

void epd_power_on(void)
{
    gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << EPD_PIN_PWR,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&pwr);
    pin(EPD_PIN_PWR, 1);            /* panel supply enable */
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void epd_wr_bus(uint8_t dat)
{
    pin(EPD_PIN_CS, 0);
    for (int i = 0; i < 8; i++) {
        pin(EPD_PIN_SCK, 0);
        pin(EPD_PIN_MOSI, (dat & 0x80) ? 1 : 0);
        pin(EPD_PIN_SCK, 1);
        dat <<= 1;
    }
    pin(EPD_PIN_CS, 1);
}

static void epd_cmd(uint8_t reg)
{
    pin(EPD_PIN_DC, 0);
    epd_wr_bus(reg);
    pin(EPD_PIN_DC, 1);
}

static void epd_data(uint8_t dat)
{
    pin(EPD_PIN_DC, 1);
    epd_wr_bus(dat);
}

static void epd_wait_busy(void)
{
    /* BUSY is high while the controller is working */
    while (gpio_get_level(EPD_PIN_BUSY)) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    esp_rom_delay_us(100);
}

/* ------------------------------------------------------------------ */
/* Panel init / refresh (mirrors Elecrow EPD_Init.cpp)                 */
/* ------------------------------------------------------------------ */

static void epd_hw_sw_reset(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    pin(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    pin(EPD_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    pin(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    epd_wait_busy();
    epd_cmd(0x12);                  /* SWRESET */
    epd_wait_busy();
}

void epd_init(void)
{
    epd_gpio_init();
    epd_hw_sw_reset();

    epd_cmd(0x01);                  /* driver output control: 250 gates */
    epd_data(0xF9);
    epd_data(0x00);
    epd_data(0x00);

    epd_cmd(0x11);                  /* data entry mode: x+ y+ */
    epd_data(0x03);

    epd_cmd(0x44);                  /* RAM x window: 0..15 (16 bytes)  */
    epd_data(0x00);
    epd_data(0x0F);

    epd_cmd(0x45);                  /* RAM y window: 0..249            */
    epd_data(0x00);
    epd_data(0x00);
    epd_data(0xF9);
    epd_data(0x00);

    epd_cmd(0x3C);                  /* border waveform                 */
    epd_data(0x01);
    epd_wait_busy();

    epd_cmd(0x18);                  /* internal temperature sensor     */
    epd_data(0x80);

    epd_cmd(0x4E);                  /* RAM x counter                   */
    epd_data(0x00);
    epd_cmd(0x4F);                  /* RAM y counter                   */
    epd_data(0x00);
    epd_data(0x00);

    epd_wait_busy();
}

void epd_sleep(void)
{
    epd_cmd(0x10);                  /* deep sleep mode 1 */
    epd_data(0x01);
    epd_cmd(0x3C);
    epd_data(0x01);
    vTaskDelay(pdMS_TO_TICKS(20));
}

void epd_update_full(void)
{
    epd_cmd(0x22);
    epd_data(0xF4);                 /* clk+dcdc on, load LUT, mode 1 */
    epd_cmd(0x20);
    epd_wait_busy();
}

void epd_update_partial(void)
{
    epd_cmd(0x22);
    epd_data(0xFC);                 /* mode 2 (partial), keep dcdc on */
    epd_cmd(0x20);
    epd_wait_busy();

    epd_cmd(0x3C);
    epd_data(0x01);
}

void epd_clear_prev_ram(void)
{
    epd_cmd(0x26);                  /* "previous image" RAM */
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        epd_data(0xFF);             /* white */
    }
    epd_wait_busy();
}

void epd_write_prev_image(const uint8_t *fb)
{
    epd_cmd(0x26);                  /* "previous image" RAM */
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        epd_data((uint8_t)~fb[i]);  /* same fb->RAM inversion as epd_write_image */
    }
    epd_wait_busy();
}

void epd_fill(uint8_t color)
{
    uint8_t v = (color == EPD_COLOR_BLACK) ? 0x00 : 0xFF;
    epd_cmd(0x3C);
    epd_data((color == EPD_COLOR_BLACK) ? 0x00 : 0x01);
    epd_cmd(0x24);
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        epd_data(v);
    }
    epd_wait_busy();
}

void epd_write_image(const uint8_t *fb)
{
    epd_cmd(0x3C);
    epd_data(0x01);
    epd_cmd(0x24);
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        epd_data((uint8_t)~fb[i]);  /* fb: 1=black, panel RAM: 0=black */
    }
}

/* ------------------------------------------------------------------ */
/* Framebuffer drawing (landscape 250x122, bit set = black)            */
/* ------------------------------------------------------------------ */

void fb_clear(uint8_t *fb, uint8_t color)
{
    memset(fb, (color == EPD_COLOR_BLACK) ? 0xFF : 0x00, EPD_BUF_SIZE);
}

void fb_set_pixel(uint8_t *fb, int x, int y, uint8_t color)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    /* Map landscape (x,y) onto native portrait RAM:
       column = y (0..121), gate line = 249 - x, 16 bytes per line. */
    int xp = y;
    int yp = (EPD_WIDTH - 1) - x;
    uint32_t addr = (uint32_t)(xp / 8) + (uint32_t)yp * EPD_LINE_BYTES;
    uint8_t mask = 0x80 >> (xp % 8);

    if (color == EPD_COLOR_BLACK) {
        fb[addr] |= mask;
    } else {
        fb[addr] &= ~mask;
    }
}

void fb_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            fb_set_pixel(fb, i, j, color);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Seven-segment digits                                                */
/* ------------------------------------------------------------------ */

/* Segment bits: A=top, B=top-right, C=bottom-right, D=bottom,
                 E=bottom-left, F=top-left, G=middle */
enum { SA = 1, SB = 2, SC = 4, SD = 8, SE = 16, SF = 32, SG = 64 };

static const uint8_t seg_map[10] = {
    /* 0 */ SA | SB | SC | SD | SE | SF,
    /* 1 */ SB | SC,
    /* 2 */ SA | SB | SG | SE | SD,
    /* 3 */ SA | SB | SG | SC | SD,
    /* 4 */ SF | SG | SB | SC,
    /* 5 */ SA | SF | SG | SC | SD,
    /* 6 */ SA | SF | SG | SE | SC | SD,
    /* 7 */ SA | SB | SC,
    /* 8 */ SA | SB | SC | SD | SE | SF | SG,
    /* 9 */ SA | SB | SC | SD | SF | SG,
};

void fb_draw_7seg_digit(uint8_t *fb, int x, int y, int w, int h, int t, int d)
{
    if (d < 0 || d > 9) {
        return;
    }
    uint8_t s = seg_map[d];
    int half = (h - t) / 2;         /* y offset of the middle segment */

    if (s & SA) fb_fill_rect(fb, x + t, y,             w - 2 * t, t, EPD_COLOR_BLACK);
    if (s & SG) fb_fill_rect(fb, x + t, y + half,      w - 2 * t, t, EPD_COLOR_BLACK);
    if (s & SD) fb_fill_rect(fb, x + t, y + h - t,     w - 2 * t, t, EPD_COLOR_BLACK);
    if (s & SF) fb_fill_rect(fb, x,         y + t,        t, half - t, EPD_COLOR_BLACK);
    if (s & SB) fb_fill_rect(fb, x + w - t, y + t,        t, half - t, EPD_COLOR_BLACK);
    if (s & SE) fb_fill_rect(fb, x,         y + half + t, t, h - half - 2 * t, EPD_COLOR_BLACK);
    if (s & SC) fb_fill_rect(fb, x + w - t, y + half + t, t, h - half - 2 * t, EPD_COLOR_BLACK);
}

void fb_draw_colon(uint8_t *fb, int x, int y, int size)
{
    fb_fill_rect(fb, x, y,            size, size, EPD_COLOR_BLACK);
    fb_fill_rect(fb, x, y + 3 * size, size, size, EPD_COLOR_BLACK);
}

/* ------------------------------------------------------------------ */
/* AM/PM indicator (5x7 bitmap font, letters A/P/M only)               */
/* ------------------------------------------------------------------ */

#define AMPM_SCALE  2   /* glyph pixel size on screen */
#define AMPM_GAP    2   /* px between the two letters */

/* Each row is 5 bits wide (bit4 = leftmost column). */
static const uint8_t FONT5X7_A[7] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
static const uint8_t FONT5X7_M[7] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
static const uint8_t FONT5X7_P[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };

static void fb_draw_glyph(uint8_t *fb, int x, int y, int scale, const uint8_t rows[7])
{
    for (int r = 0; r < 7; r++) {
        for (int c = 0; c < 5; c++) {
            if (rows[r] & (0x10 >> c)) {
                fb_fill_rect(fb, x + c * scale, y + r * scale, scale, scale,
                             EPD_COLOR_BLACK);
            }
        }
    }
}

void fb_draw_ampm(uint8_t *fb, int x, int y, bool is_pm)
{
    const uint8_t *first = is_pm ? FONT5X7_P : FONT5X7_A;
    fb_draw_glyph(fb, x, y, AMPM_SCALE, first);
    fb_draw_glyph(fb, x + (5 * AMPM_SCALE + AMPM_GAP), y, AMPM_SCALE, FONT5X7_M);
}
