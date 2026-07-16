/*
 * gfxfont.h - GFX font container format, taken from Adafruit-GFX-Library
 * (BSD license) so the bundled font tables in main/fonts/ can be used
 * unmodified. See main/gfx.h for the ESP-IDF drawing API that consumes
 * these fonts (a C port of Adafruit_GFX's custom-font code path, which
 * is what GxEPD2 itself renders text with on Arduino).
 */
#pragma once

#include <stdint.h>

/* Font data stored PER GLYPH */
typedef struct {
    uint16_t bitmapOffset; /* pointer into GFXfont->bitmap */
    uint8_t width;         /* bitmap dimensions in pixels */
    uint8_t height;        /* bitmap dimensions in pixels */
    uint8_t xAdvance;      /* distance to advance cursor (x axis) */
    int8_t xOffset;        /* x dist from cursor pos to UL corner */
    int8_t yOffset;        /* y dist from cursor pos to UL corner */
} GFXglyph;

/* Data stored for FONT AS A WHOLE */
typedef struct {
    const uint8_t *bitmap;  /* glyph bitmaps, concatenated */
    const GFXglyph *glyph;  /* glyph array */
    uint16_t first;         /* ASCII extents (first char) */
    uint16_t last;          /* ASCII extents (last char) */
    uint8_t yAdvance;       /* newline distance (y axis) */
} GFXfont;
