/*
 * lvgl_ui.h - LVGL-based clock face for the 250x122 e-paper panel.
 *
 * LVGL renders through a monochrome (1bpp) display driver into a private
 * scratch buffer; lvgl_ui_render() drives one layout+draw pass and returns
 * a pointer to an EPD_BUF_SIZE buffer already in the SSD1680 RAM layout,
 * ready to hand to epd_write_image()/epd_write_prev_image().
 */
#pragma once

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the LVGL display driver and builds the clock widgets. Call once,
 * after epd_power_on()/epd_init() so the panel geometry is known. */
void lvgl_ui_init(void);

/* Updates the time/date/AM-PM text for the given local time, runs an LVGL
 * render pass, and returns the resulting EPD_BUF_SIZE framebuffer. */
const uint8_t *lvgl_ui_render(const struct tm *t);

#ifdef __cplusplus
}
#endif
