/*
 * lvgl_ui.c - LVGL clock face for the CrowPanel 2.13" e-paper (SSD1680).
 *
 * LVGL renders normally in 16bpp into a private EPD_WIDTH x EPD_HEIGHT
 * buffer; flush_cb() thresholds each pixel's brightness to black/white
 * and writes it into the panel's 1bpp framebuffer via fb_set_pixel().
 * (LVGL's native LV_COLOR_DEPTH=1 + set_px_cb path was tried first, but
 * its label/text draw routines are a much less exercised code path and
 * crashed; full-color render + manual threshold is the standard, well
 * tested way to drive a monochrome panel from LVGL v8.)
 */
#include "lvgl_ui.h"

#include <stdbool.h>

#include "esp_timer.h"
#include "lvgl.h"
#include "epd213.h"

static lv_color_t s_lv_draw_buf[EPD_WIDTH * EPD_HEIGHT];
static uint8_t s_epd_fb[EPD_BUF_SIZE];
static int64_t s_last_tick_us;

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

static lv_obj_t *s_time_label;
static lv_obj_t *s_ampm_label;
static lv_obj_t *s_date_label;

/* ------------------------------------------------------------------ */
/* Display driver: full-color render, thresholded to 1bpp on flush     */
/* ------------------------------------------------------------------ */

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            bool white = lv_color_brightness(*color_p) > 128;
            fb_set_pixel(s_epd_fb, x, y, white ? EPD_COLOR_WHITE : EPD_COLOR_BLACK);
            color_p++;
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
    s_last_tick_us = esp_timer_get_time();

    lv_disp_draw_buf_init(&s_draw_buf, s_lv_draw_buf, NULL,
                           EPD_WIDTH * EPD_HEIGHT);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.hor_res = EPD_WIDTH;
    s_disp_drv.ver_res = EPD_HEIGHT;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.full_refresh = 1;
    lv_disp_drv_register(&s_disp_drv);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_black(), 0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 2);

    /* Rounded AM/PM badge, top-right corner */
    s_ampm_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_ampm_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ampm_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_ampm_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ampm_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ampm_label, 4, 0);
    lv_obj_set_style_pad_hor(s_ampm_label, 5, 0);
    lv_obj_set_style_pad_ver(s_ampm_label, 2, 0);
    lv_obj_align(s_ampm_label, LV_ALIGN_TOP_RIGHT, -4, 6);

    /* Divider between the time and the date */
    lv_obj_t *divider = lv_obj_create(scr);
    lv_obj_remove_style_all(divider);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_size(divider, EPD_WIDTH - 60, 2);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 84);

    s_date_label = lv_label_create(scr);
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

    int64_t now_us = esp_timer_get_time();
    lv_tick_inc((uint32_t)((now_us - s_last_tick_us) / 1000));
    s_last_tick_us = now_us;

    lv_timer_handler();

    return s_epd_fb;
}
