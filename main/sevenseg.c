/*
 * sevenseg.c - Seven-segment digit/colon drawing, built out of
 * gfx_fill_rect() calls (no bevels/anti-aliasing, matching this project's
 * other 1bpp e-paper drawing).
 *
 * Segment naming follows the usual LED-display convention:
 *     _a_
 *    f   b
 *     -g-
 *    e   c
 *     -d-
 */
#include "sevenseg.h"

/* Bit order per digit: a b c d e f g */
static const uint8_t SEGMENTS[10] = {
    0b1111110, /* 0 */
    0b0110000, /* 1 */
    0b1101101, /* 2 */
    0b1111001, /* 3 */
    0b0110011, /* 4 */
    0b1011011, /* 5 */
    0b1011111, /* 6 */
    0b1110000, /* 7 */
    0b1111111, /* 8 */
    0b1111011, /* 9 */
};

void sevenseg_draw_digit(gfx_t *g, int16_t x, int16_t y, int16_t w, int16_t h,
                          int16_t thickness, int digit, uint8_t color) {
  if (digit < 0 || digit > 9) {
    return;
  }
  uint8_t segs = SEGMENTS[digit];
  int16_t s = thickness;
  int16_t half_s = s / 2;
  int16_t half = h / 2;
  int16_t vlen = half - s - half_s;
  if (vlen < 1) {
    vlen = 1;
  }

  if (segs & 0b1000000) { /* a: top */
    gfx_fill_rect(g, x + s, y, w - 2 * s, s, color);
  }
  if (segs & 0b0100000) { /* b: top-right */
    gfx_fill_rect(g, x + w - s, y + s, s, vlen, color);
  }
  if (segs & 0b0010000) { /* c: bottom-right */
    gfx_fill_rect(g, x + w - s, y + half + half_s, s, vlen, color);
  }
  if (segs & 0b0001000) { /* d: bottom */
    gfx_fill_rect(g, x + s, y + h - s, w - 2 * s, s, color);
  }
  if (segs & 0b0000100) { /* e: bottom-left */
    gfx_fill_rect(g, x, y + half + half_s, s, vlen, color);
  }
  if (segs & 0b0000010) { /* f: top-left */
    gfx_fill_rect(g, x, y + s, s, vlen, color);
  }
  if (segs & 0b0000001) { /* g: middle */
    gfx_fill_rect(g, x + s, y + half - half_s, w - 2 * s, s, color);
  }
}

void sevenseg_draw_colon(gfx_t *g, int16_t cx, int16_t y, int16_t h,
                          int16_t dot_size, uint8_t color) {
  int16_t x = cx - dot_size / 2;
  int16_t top_y = y + h / 3 - dot_size / 2;
  int16_t bottom_y = y + (2 * h) / 3 - dot_size / 2;
  gfx_fill_rect(g, x, top_y, dot_size, dot_size, color);
  gfx_fill_rect(g, x, bottom_y, dot_size, dot_size, color);
}
