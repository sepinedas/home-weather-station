/*
 * gfx.c - C port of the Adafruit_GFX drawing primitives GxEPD2 relies on
 * for graphics/text (GxEPD2_GFX : public Adafruit_GFX on Arduino).
 * Algorithms below (Bresenham line/circle, GFXfont glyph rasterization,
 * cursor advance, text bounds) are translated line-for-line from
 * Adafruit_GFX.cpp (BSD license); only the storage backend changed, from
 * a 16-bit color writePixel() to this project's 1bpp fb_set_pixel().
 */
#include "gfx.h"
#include "epd213.h"

#include <stdlib.h>

void gfx_init(gfx_t *g, uint8_t *fb, int16_t width, int16_t height)
{
    g->fb = fb;
    g->width = width;
    g->height = height;
    g->cursor_x = 0;
    g->cursor_y = 0;
    g->font = NULL;
}

void gfx_set_font(gfx_t *g, const GFXfont *font)
{
    g->font = font;
}

void gfx_set_cursor(gfx_t *g, int16_t x, int16_t y)
{
    g->cursor_x = x;
    g->cursor_y = y;
}

void gfx_draw_pixel(gfx_t *g, int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= g->width || y < 0 || y >= g->height) {
        return;
    }
    fb_set_pixel(g->fb, x, y, color);
}

void gfx_draw_fast_hline(gfx_t *g, int16_t x, int16_t y, int16_t w, uint8_t color)
{
    fb_fill_rect(g->fb, x, y, w, 1, color);
}

void gfx_draw_fast_vline(gfx_t *g, int16_t x, int16_t y, int16_t h, uint8_t color)
{
    fb_fill_rect(g->fb, x, y, 1, h, color);
}

void gfx_fill_rect(gfx_t *g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    fb_fill_rect(g->fb, x, y, w, h, color);
}

void gfx_draw_rect(gfx_t *g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    gfx_draw_fast_hline(g, x, y, w, color);
    gfx_draw_fast_hline(g, x, y + h - 1, w, color);
    gfx_draw_fast_vline(g, x, y, h, color);
    gfx_draw_fast_vline(g, x + w - 1, y, h, color);
}

/* Bresenham, ported from Adafruit_GFX::writeLine() */
static void gfx_write_line(gfx_t *g, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    int16_t steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        int16_t t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        int16_t t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }

    int16_t dx = x1 - x0;
    int16_t dy = abs(y1 - y0);
    int16_t err = dx / 2;
    int16_t ystep = (y0 < y1) ? 1 : -1;

    for (; x0 <= x1; x0++) {
        if (steep) {
            gfx_draw_pixel(g, y0, x0, color);
        } else {
            gfx_draw_pixel(g, x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

void gfx_draw_line(gfx_t *g, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    if (x0 == x1) {
        if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
        gfx_draw_fast_vline(g, x0, y0, y1 - y0 + 1, color);
    } else if (y0 == y1) {
        if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
        gfx_draw_fast_hline(g, x0, y0, x1 - x0 + 1, color);
    } else {
        gfx_write_line(g, x0, y0, x1, y1, color);
    }
}

void gfx_draw_circle(gfx_t *g, int16_t x0, int16_t y0, int16_t r, uint8_t color)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    gfx_draw_pixel(g, x0, y0 + r, color);
    gfx_draw_pixel(g, x0, y0 - r, color);
    gfx_draw_pixel(g, x0 + r, y0, color);
    gfx_draw_pixel(g, x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        gfx_draw_pixel(g, x0 + x, y0 + y, color);
        gfx_draw_pixel(g, x0 - x, y0 + y, color);
        gfx_draw_pixel(g, x0 + x, y0 - y, color);
        gfx_draw_pixel(g, x0 - x, y0 - y, color);
        gfx_draw_pixel(g, x0 + y, y0 + x, color);
        gfx_draw_pixel(g, x0 - y, y0 + x, color);
        gfx_draw_pixel(g, x0 + y, y0 - x, color);
        gfx_draw_pixel(g, x0 - y, y0 - x, color);
    }
}

/* corners: bit0=upper-left half, bit1=upper-right half (mirrors Adafruit_GFX) */
static void gfx_fill_circle_helper(gfx_t *g, int16_t x0, int16_t y0, int16_t r,
                                   uint8_t corners, int16_t delta, uint8_t color)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;
    int16_t px = x;
    int16_t py = y;

    delta++;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        if (x < (y + 1)) {
            if (corners & 1) gfx_draw_fast_vline(g, x0 + x, y0 - y, 2 * y + delta, color);
            if (corners & 2) gfx_draw_fast_vline(g, x0 - x, y0 - y, 2 * y + delta, color);
        }
        if (y != py) {
            if (corners & 1) gfx_draw_fast_vline(g, x0 + py, y0 - px, 2 * px + delta, color);
            if (corners & 2) gfx_draw_fast_vline(g, x0 - py, y0 - px, 2 * px + delta, color);
            py = y;
        }
        px = x;
    }
}

void gfx_fill_circle(gfx_t *g, int16_t x0, int16_t y0, int16_t r, uint8_t color)
{
    gfx_draw_fast_vline(g, x0, y0 - r, 2 * r + 1, color);
    gfx_fill_circle_helper(g, x0, y0, r, 3, 0, color);
}

/* Ported from Adafruit_GFX::drawChar() custom-font branch. */
void gfx_draw_char(gfx_t *g, int16_t x, int16_t y, unsigned char c, uint8_t color)
{
    if (!g->font) {
        return;
    }
    if (c < g->font->first || c > g->font->last) {
        return;
    }

    const GFXglyph *glyph = &g->font->glyph[c - g->font->first];
    const uint8_t *bitmap = g->font->bitmap;

    uint16_t bo = glyph->bitmapOffset;
    uint8_t w = glyph->width, h = glyph->height;
    int8_t xo = glyph->xOffset, yo = glyph->yOffset;
    uint8_t bits = 0, bit = 0;

    for (uint8_t yy = 0; yy < h; yy++) {
        for (uint8_t xx = 0; xx < w; xx++) {
            if (!(bit++ & 7)) {
                bits = bitmap[bo++];
            }
            if (bits & 0x80) {
                gfx_draw_pixel(g, x + xo + xx, y + yo + yy, color);
            }
            bits <<= 1;
        }
    }
}

/* Ported from Adafruit_GFX::charBounds() custom-font branch. */
static void gfx_char_bounds(gfx_t *g, unsigned char c, int16_t *x, int16_t *y,
                             int16_t *minx, int16_t *miny, int16_t *maxx, int16_t *maxy)
{
    if (c == '\n') {
        *x = 0;
        *y += g->font->yAdvance;
        return;
    }
    if (c == '\r') {
        return;
    }
    if (c < g->font->first || c > g->font->last) {
        return;
    }

    const GFXglyph *glyph = &g->font->glyph[c - g->font->first];
    int16_t x1 = *x + glyph->xOffset;
    int16_t y1 = *y + glyph->yOffset;
    int16_t x2 = x1 + glyph->width - 1;
    int16_t y2 = y1 + glyph->height - 1;

    if (x1 < *minx) *minx = x1;
    if (y1 < *miny) *miny = y1;
    if (x2 > *maxx) *maxx = x2;
    if (y2 > *maxy) *maxy = y2;
    *x += glyph->xAdvance;
}

void gfx_get_text_bounds(gfx_t *g, const char *str, int16_t x, int16_t y,
                          int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h)
{
    int16_t minx = 0x7FFF, miny = 0x7FFF, maxx = -1, maxy = -1;
    *x1 = x;
    *y1 = y;
    *w = *h = 0;

    if (!g->font) {
        return;
    }

    unsigned char c;
    while ((c = (unsigned char)*str++)) {
        gfx_char_bounds(g, c, &x, &y, &minx, &miny, &maxx, &maxy);
    }

    if (maxx >= minx) {
        *x1 = minx;
        *w = maxx - minx + 1;
    }
    if (maxy >= miny) {
        *y1 = miny;
        *h = maxy - miny + 1;
    }
}

/* Ported from Adafruit_GFX::write() custom-font branch (no line wrap - our
 * strings are always single-line labels drawn at an explicit cursor). */
void gfx_print(gfx_t *g, const char *str, uint8_t color)
{
    if (!g->font) {
        return;
    }

    unsigned char c;
    while ((c = (unsigned char)*str++)) {
        if (c == '\n') {
            g->cursor_x = 0;
            g->cursor_y += g->font->yAdvance;
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (c < g->font->first || c > g->font->last) {
            continue;
        }

        const GFXglyph *glyph = &g->font->glyph[c - g->font->first];
        if (glyph->width > 0 && glyph->height > 0) {
            gfx_draw_char(g, g->cursor_x, g->cursor_y, c, color);
        }
        g->cursor_x += glyph->xAdvance;
    }
}
