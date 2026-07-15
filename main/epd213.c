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
