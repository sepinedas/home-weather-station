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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pins (CrowPanel 2.13" DIE01021S) */
#define EPD_PIN_PWR   7
#define EPD_PIN_BUSY  9
#define EPD_PIN_RES   10
#define EPD_PIN_MOSI  11
#define EPD_PIN_SCK   12
#define EPD_PIN_DC    13
#define EPD_PIN_CS    14

/* Panel geometry (landscape) */
#define EPD_WIDTH        250
#define EPD_HEIGHT       122
#define EPD_LINE_BYTES   16                       /* 122 px -> 16 bytes per gate line */
#define EPD_BUF_SIZE     (EPD_LINE_BYTES * 250)   /* 4000 bytes */

#define EPD_COLOR_WHITE  0
#define EPD_COLOR_BLACK  1

/* Power / lifecycle */
void epd_power_on(void);          /* drive GPIO7 high (panel supply)      */
void epd_init(void);              /* HW+SW reset and register init        */
void epd_sleep(void);             /* deep sleep mode 1 (RAM retained)     */

/* Refresh */
void epd_fill(uint8_t color);                 /* fill controller RAM directly */
void epd_clear_prev_ram(void);                /* clear 0x26 RAM (needed once
                                                 before partial refresh)      */
void epd_write_image(const uint8_t *fb);      /* send framebuffer to 0x24     */
void epd_update_full(void);                   /* global (flashing) refresh    */
void epd_update_partial(void);                /* fast partial refresh         */

/* Framebuffer drawing (1 = black). Buffer must be EPD_BUF_SIZE bytes. */
void fb_clear(uint8_t *fb, uint8_t color);
void fb_set_pixel(uint8_t *fb, int x, int y, uint8_t color);
void fb_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color);

/* Seven-segment digit renderer (no font tables needed).
 * x,y   : top-left corner
 * w,h   : digit size in pixels
 * t     : segment thickness in pixels
 * d     : 0..9
 */
void fb_draw_7seg_digit(uint8_t *fb, int x, int y, int w, int h, int t, int d);
void fb_draw_colon(uint8_t *fb, int x, int y, int size);

#ifdef __cplusplus
}
#endif
