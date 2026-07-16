/*
 * epd213.h - Driver for the Elecrow CrowPanel ESP32-S3 2.13" e-paper HMI
 *            (122 x 250, SSD1680Z, bit-banged 4-wire SPI)
 *
 * Pin map taken from the official Elecrow demo code:
 *   SCK  = GPIO12, MOSI = GPIO11, RES = GPIO10,
 *   DC   = GPIO13, CS   = GPIO14, BUSY = GPIO9,
 *   Panel power enable = GPIO7 (must be driven HIGH)
 *
 * Coordinate system exposed to the application: LANDSCAPE,
 *   x = 0..249 (left -> right), y = 0..121 (top -> bottom)
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pins (CrowPanel 2.13" DIE01021S) */
#define EPD_PIN_PWR 7
#define EPD_PIN_BUSY 9
#define EPD_PIN_RES 10
#define EPD_PIN_MOSI 11
#define EPD_PIN_SCK 12
#define EPD_PIN_DC 13
#define EPD_PIN_CS 14

/* Panel geometry (landscape) */
#define EPD_WIDTH 250
#define EPD_HEIGHT 122
#define EPD_LINE_BYTES 16 /* 122 px -> 16 bytes per gate line */
#define EPD_BUF_SIZE (EPD_LINE_BYTES * 250) /* 4000 bytes */

#define EPD_COLOR_WHITE 0
#define EPD_COLOR_BLACK 1

/* Power / lifecycle */
void epd_power_on(void); /* drive GPIO7 high (panel supply)      */
void epd_init(void);     /* HW+SW reset and register init        */
void epd_sleep(void);    /* deep sleep mode 1 (RAM retained)     */

/* Refresh */
void epd_fill(uint8_t color);                 /* fill controller RAM directly */
void epd_clear_prev_ram(void);                /* blank 0x26 RAM (needed once
                                                 before the very first
                                                 partial refresh)             */
void epd_write_image(const uint8_t *fb);      /* send framebuffer to 0x24     */
void epd_write_prev_image(const uint8_t *fb); /* sync 0x26 to fb so the next
                                                 partial refresh diffs
                                                 against what's actually on
                                                 the panel (avoids ghosting
                                                 of previously drawn
                                                 segments)                    */
void epd_update_full(void);                   /* global (flashing) refresh    */
void epd_update_partial(void);                /* fast partial refresh         */
void epd_update_fast(void);
void epd_flash_clean(void); /* black->white double flash to
                               more thoroughly reset pixel
                               dipoles than a single full
                               refresh; call before writing
                               new content on a full-refresh
                               cycle to cut down ghosting
                               buildup from partial
                               refreshes                    */

/* Framebuffer drawing (1 = black). Buffer must be EPD_BUF_SIZE bytes. */
void fb_clear(uint8_t *fb, uint8_t color);
void fb_clear_R26H(void);
void fb_set_pixel(uint8_t *fb, int x, int y, uint8_t color);
void fb_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color);

#ifdef __cplusplus
}
#endif
