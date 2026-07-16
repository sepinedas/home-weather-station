/*
 * gfx.h - Minimal Adafruit_GFX-style drawing API, ported to plain C for
 * ESP-IDF. This is the graphics/text layer GxEPD2 itself is built on
 * (GxEPD2_GFX derives from Adafruit_GFX on Arduino); porting it directly
 * gives the same GFXfont-based text rendering and shape primitives
 * without pulling in Arduino, Adafruit_GFX, or GxEPD2 as dependencies.
 *
 * Draw calls write into the existing epd213 framebuffer via
 * fb_set_pixel()/fb_fill_rect(), so the SSD1680 panel driver and
 * refresh/ghosting logic in epd213.c are untouched.
 */
#pragma once

#include <stdint.h>
#include "gfxfont.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *fb;
    int16_t width;
    int16_t height;
    int16_t cursor_x;
    int16_t cursor_y;
    const GFXfont *font;
} gfx_t;

void gfx_init(gfx_t *g, uint8_t *fb, int16_t width, int16_t height);
void gfx_set_font(gfx_t *g, const GFXfont *font);
void gfx_set_cursor(gfx_t *g, int16_t x, int16_t y);

void gfx_draw_pixel(gfx_t *g, int16_t x, int16_t y, uint8_t color);
void gfx_draw_fast_hline(gfx_t *g, int16_t x, int16_t y, int16_t w, uint8_t color);
void gfx_draw_fast_vline(gfx_t *g, int16_t x, int16_t y, int16_t h, uint8_t color);
void gfx_draw_line(gfx_t *g, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void gfx_draw_rect(gfx_t *g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gfx_fill_rect(gfx_t *g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void gfx_draw_circle(gfx_t *g, int16_t x0, int16_t y0, int16_t r, uint8_t color);
void gfx_fill_circle(gfx_t *g, int16_t x0, int16_t y0, int16_t r, uint8_t color);

/* Requires gfx_set_font() first - there is no 'classic' built-in font. */
void gfx_draw_char(gfx_t *g, int16_t x, int16_t y, unsigned char c, uint8_t color);
void gfx_print(gfx_t *g, const char *str, uint8_t color); /* draws at cursor, advances it */
void gfx_get_text_bounds(gfx_t *g, const char *str, int16_t x, int16_t y,
                          int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h);

#ifdef __cplusplus
}
#endif
