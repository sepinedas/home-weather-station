/*
 * CrowPanel ESP32-S3 2.13" e-paper clock (ESP-IDF)
 *
 * - Connects to WiFi and syncs time via SNTP
 * - Draws HH:MM (12-hour, with AM/PM) and the date using LVGL labels
 * - Partial refresh every minute, full refresh every N partials
 *   to clean up ghosting
 * - UI built with LVGL (lvgl/lvgl); panel power/SPI/refresh delegated
 *   to the antunesls/crowpanel_epaper_driver_component managed
 *   component. LVGL's software renderer draws into an 8-bit grayscale
 *   (L8) buffer, and our flush callback thresholds each pixel into the
 *   component's 1bpp Paint image via Paint_SetPixel().
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "epaper_driver.h"
#include "lvgl.h"

static const char *TAG = "epaper_clock";

/* EPD_Display_Part()'s RAM-window addressing doesn't appear to account
 * for this panel's logical->physical rotation (see README caveat), so
 * default to always doing the full (flashing) refresh until that's
 * verified safe on real hardware. Raise this once partial refresh is
 * confirmed to render correctly. */
#define FULL_REFRESH_EVERY  1

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT  BIT0

static uint8_t s_fb[((EPD_W + 7) / 8) * EPD_H];        /* packed 1bpp, for the crowpanel component */
static uint8_t s_lv_buf[EPD_W * EPD_H] __attribute__((aligned(4)));  /* LVGL L8 render buffer */

static lv_display_t *s_disp;
static lv_obj_t *s_time_label;
static lv_obj_t *s_ampm_label;
static lv_obj_t *s_date_label;

/* ------------------------------------------------------------------ */
/* WiFi + SNTP                                                         */
/* ------------------------------------------------------------------ */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, CONFIG_CLOCK_WIFI_SSID, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, CONFIG_CLOCK_WIFI_PASSWORD,
            sizeof(sta.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static bool sync_time(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    /* Wait until the year looks sane */
    for (int i = 0; i < 30; i++) {
        time_t now = 0;
        struct tm tm_now = { 0 };
        time(&now);
        localtime_r(&now, &tm_now);
        if (tm_now.tm_year >= (2024 - 1900)) {
            return true;
        }
        ESP_LOGI(TAG, "waiting for SNTP... (%d)", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* LVGL <-> e-paper bridge                                             */
/* ------------------------------------------------------------------ */

static uint32_t lv_tick_get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* LVGL renders into an L8 (1 byte/pixel grayscale) buffer; luminance 0
 * is black and 255 is white. Threshold each pixel into the crowpanel
 * component's packed 1bpp Paint image (s_fb), which app_main() then
 * pushes to the panel via EPD_Display()/EPD_Display_Part(). */
static void epd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    for (int32_t row = 0; row < h; row++) {
        for (int32_t col = 0; col < w; col++) {
            uint8_t gray = px_map[row * w + col];
            Paint_SetPixel(area->x1 + col, area->y1 + row, gray < 128 ? BLACK : WHITE);
        }
    }

    lv_display_flush_ready(disp);
}

static void lvgl_init(void)
{
    lv_init();
    lv_tick_set_cb(lv_tick_get_ms);

    Paint_NewImage(s_fb, EPD_W, EPD_H, ROTATE_0, WHITE);

    s_disp = lv_display_create(EPD_W, EPD_H);
    lv_display_set_default(s_disp);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_L8);
    lv_display_set_buffers(s_disp, s_lv_buf, NULL, sizeof(s_lv_buf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_disp, epd_flush_cb);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    s_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_black(), 0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 2);

    s_date_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_black(), 0);
    lv_obj_align(s_date_label, LV_ALIGN_BOTTOM_LEFT, 4, -6);

    s_ampm_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_ampm_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_ampm_label, lv_color_black(), 0);
    lv_obj_align(s_ampm_label, LV_ALIGN_BOTTOM_RIGHT, -4, -6);
}

static void update_clock_ui(const struct tm *t)
{
    bool is_pm = t->tm_hour >= 12;
    int hh = t->tm_hour % 12;
    if (hh == 0) {
        hh = 12;
    }

    lv_label_set_text_fmt(s_time_label, "%d:%02d", hh, t->tm_min);
    lv_label_set_text_fmt(s_date_label, "%02d-%02d-%04d",
                          t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
    lv_label_set_text(s_ampm_label, is_pm ? "PM" : "AM");

    /* Render + flush synchronously so s_fb is fully up to date before
     * the caller pushes it to the panel. */
    lv_refr_now(s_disp);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

void app_main(void)
{
    /* NVS is required by WiFi */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Timezone (POSIX TZ string from menuconfig) */
    setenv("TZ", CONFIG_CLOCK_TZ, 1);
    tzset();

    wifi_start();
    ESP_LOGI(TAG, "waiting for WiFi...");
    xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));

    if (!sync_time()) {
        ESP_LOGW(TAG, "SNTP failed, clock will start from epoch");
    }

    /* Bring up the panel: power, GPIO/SPI, then blank it out once
     * (EPD_Clear() runs its own EPD_Init() and does a full refresh). */
    EPD_GPIOInit();
    EPD_Clear();

    lvgl_init();

    int part_count = 0;
    int last_min = -1;

    while (1) {
        time_t now;
        struct tm t;
        time(&now);
        localtime_r(&now, &t);

        if (t.tm_min != last_min) {
            last_min = t.tm_min;
            update_clock_ui(&t);           /* renders via LVGL into s_fb */

            EPD_Init();                    /* wake from deep sleep */

            if (part_count == 0) {
                EPD_Display(s_fb);          /* full (flashing) refresh */
            } else {
                EPD_Display_Part(0, 0, EPD_W, EPD_H, s_fb);  /* fast partial refresh */
            }
            part_count = (part_count + 1) % FULL_REFRESH_EVERY;

            EPD_Sleep();
            ESP_LOGI(TAG, "displayed %02d:%02d", t.tm_hour, t.tm_min);
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
