/*
 * sevenseg.h - Seven-segment digit/colon drawing on top of gfx.h, for
 * rendering a digital-clock-style HH:MM instead of a proportional font.
 */
#pragma once

#include <stdint.h>
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draws digit (0-9) as a seven-segment glyph in the box (x, y, w, h),
 * with each segment 'thickness' px wide. */
void sevenseg_draw_digit(gfx_t *g, int16_t x, int16_t y, int16_t w, int16_t h,
                          int16_t thickness, int digit, uint8_t color);

/* Draws a clock colon (two stacked dots) sized to a digit box of the
 * given height, horizontally centered on cx. */
void sevenseg_draw_colon(gfx_t *g, int16_t cx, int16_t y, int16_t h,
                          int16_t dot_size, uint8_t color);

#ifdef __cplusplus
}
#endif
