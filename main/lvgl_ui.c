/*
 * lvgl_ui.c - LVGL clock face for the CrowPanel 2.13" e-paper (SSD1680).
 *
 * The panel is a true 1bpp display, so LVGL is configured for
 * LV_COLOR_DEPTH=1 and driven through set_px_cb() the same way monochrome
 * OLED ports (e.g. SSD1306) do: LVGL's software renderer calls set_px_cb()
 * per pixel instead of writing lv_color_t values into the draw buffer, and
 * flush_cb() only needs to hand the finished bitmap to the panel.
 *
 * The panel is 250px wide, which isn't a multiple of 8, so the LVGL
 * display driver is given a padded 256px horizontal resolution (keeping
 * the mono bitmap's row stride a whole number of bytes) and all widgets
 * live inside a 250x122 container placed at the screen's top-left. That
 * keeps LVGL's internal packing unambiguous while the clock face itself is
 * still centered on the real 250x122 panel.
 */
#include "lvgl_ui.h"

#include <stdbool.h>

#include "lvgl.h"
#include "epd213.h"

#define LV_HOR_RES_PADDED   256   /* next multiple of 8 >= EPD_WIDTH */
#define MONO_ROW_BYTES       (LV_HOR_RES_PADDED / 8)

static uint8_t s_mono_buf[MONO_ROW_BYTES * EPD_HEIGHT];
static uint8_t s_epd_fb[EPD_BUF_SIZE];

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

static lv_obj_t *s_time_label;
static lv_obj_t *s_ampm_label;
static lv_obj_t *s_date_label;

/* ------------------------------------------------------------------ */
/* Monochrome display driver                                           */
/* ------------------------------------------------------------------ */

static void disp_set_px_cb(lv_disp_drv_t *drv, uint8_t *buf, lv_coord_t buf_w,
                            lv_coord_t x, lv_coord_t y, lv_color_t color,
                            lv_opa_t opa)
{
    (void)drv;
    (void)opa;
    uint32_t idx = (uint32_t)y * buf_w + (uint32_t)(x >> 3);
    uint8_t bit = 7 - (x & 0x7);
    if (lv_color_brightness(color) > 128) {
        buf[idx] |= (1 << bit);    /* white */
    } else {
        buf[idx] &= (uint8_t)~(1 << bit); /* black */
    }
}

static void disp_rounder_cb(lv_disp_drv_t *drv, lv_area_t *area)
{
    (void)drv;
    area->x1 = area->x1 & ~0x7;
    area->x2 = (area->x2 & ~0x7) | 0x7;
}

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    (void)color_p;  /* pixels were already written into s_mono_buf above */

    int y2 = area->y2 < EPD_HEIGHT ? area->y2 : EPD_HEIGHT - 1;
    int x2 = area->x2 < EPD_WIDTH ? area->x2 : EPD_WIDTH - 1;

    for (int y = area->y1; y <= y2; y++) {
        for (int x = area->x1; x <= x2; x++) {
            uint32_t idx = (uint32_t)y * MONO_ROW_BYTES + (uint32_t)(x >> 3);
            bool white = s_mono_buf[idx] & (1 << (7 - (x & 0x7)));
            fb_set_pixel(s_epd_fb, x, y, white ? EPD_COLOR_WHITE : EPD_COLOR_BLACK);
        }
    }

    lv_disp_flush_ready(drv);
}

/* ------------------------------------------------------------------ */
/* Widgets                                                             */
/* ------------------------------------------------------------------ */

void lvgl_ui_init(void)
{
    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_mono_buf, NULL,
                           LV_HOR_RES_PADDED * EPD_HEIGHT);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.hor_res = LV_HOR_RES_PADDED;
    s_disp_drv.ver_res = EPD_HEIGHT;
    s_disp_drv.set_px_cb = disp_set_px_cb;
    s_disp_drv.rounder_cb = disp_rounder_cb;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.full_refresh = 1;
    lv_disp_drv_register(&s_disp_drv);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Real 250x122 panel area, pinned to the top-left of the padded
     * 256-wide screen so widget alignment is centered on the true panel. */
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 0, 0);
    lv_obj_set_size(panel, EPD_WIDTH, EPD_HEIGHT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_time_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_black(), 0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 2);

    /* Rounded AM/PM badge, top-right corner */
    s_ampm_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_ampm_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ampm_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_ampm_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ampm_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ampm_label, 4, 0);
    lv_obj_set_style_pad_hor(s_ampm_label, 5, 0);
    lv_obj_set_style_pad_ver(s_ampm_label, 2, 0);
    lv_obj_align(s_ampm_label, LV_ALIGN_TOP_RIGHT, -4, 6);

    /* Divider between the time and the date */
    lv_obj_t *divider = lv_obj_create(panel);
    lv_obj_remove_style_all(divider);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_size(divider, EPD_WIDTH - 60, 2);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 84);

    s_date_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_black(), 0);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_MID, 0, 94);
}

const uint8_t *lvgl_ui_render(const struct tm *t)
{
    static const char *const wdays[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
    };
    static const char *const months[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    bool is_pm = t->tm_hour >= 12;
    int hh = t->tm_hour % 12;
    if (hh == 0) {
        hh = 12;
    }

    lv_label_set_text_fmt(s_time_label, "%d:%02d", hh, t->tm_min);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 2);

    lv_label_set_text(s_ampm_label, is_pm ? "PM" : "AM");

    int wday = t->tm_wday >= 0 && t->tm_wday < 7 ? t->tm_wday : 0;
    int mon = t->tm_mon >= 0 && t->tm_mon < 12 ? t->tm_mon : 0;
    lv_label_set_text_fmt(s_date_label, "%s, %d %s %d",
                           wdays[wday], t->tm_mday, months[mon],
                           t->tm_year + 1900);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_MID, 0, 94);

    lv_timer_handler();

    return s_epd_fb;
}
