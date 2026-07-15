/*
 * CrowPanel ESP32-S3 2.13" e-paper clock (ESP-IDF)
 *
 * - Connects to WiFi and syncs time via SNTP
 * - Draws HH:MM (12-hour, with AM/PM) as large seven-segment digits,
 *   date below
 * - Partial refresh every minute, full refresh every N partials
 *   to clean up ghosting
 * - Rendering/panel control delegated to the
 *   antunesls/crowpanel_epaper_driver_component managed component
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
#include "nvs_flash.h"

#include "epaper_driver.h"

static const char *TAG = "epaper_clock";

#define FULL_REFRESH_EVERY  10      /* partial refreshes between full ones */

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT  BIT0

static uint8_t s_fb[((EPD_W + 7) / 8) * EPD_H];

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
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

/* Segment bits: A=top, B=top-right, C=bottom-right, D=bottom,
                 E=bottom-left, F=top-left, G=middle */
enum { SA = 1, SB = 2, SC = 4, SD = 8, SE = 16, SF = 32, SG = 64 };

static const uint8_t seg_map[10] = {
    /* 0 */ SA | SB | SC | SD | SE | SF,
    /* 1 */ SB | SC,
    /* 2 */ SA | SB | SG | SE | SD,
    /* 3 */ SA | SB | SG | SC | SD,
    /* 4 */ SF | SG | SB | SC,
    /* 5 */ SA | SF | SG | SC | SD,
    /* 6 */ SA | SF | SG | SE | SC | SD,
    /* 7 */ SA | SB | SC,
    /* 8 */ SA | SB | SC | SD | SE | SF | SG,
    /* 9 */ SA | SB | SC | SD | SF | SG,
};

/* EPD_DrawRectangle takes inclusive end coordinates; wrap it so callers
 * can keep thinking in x,y,w,h like the old fb_fill_rect helper did. */
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    EPD_DrawRectangle(x, y, x + w - 1, y + h, color, 1);
}

static void draw_7seg_digit(int x, int y, int w, int h, int t, int d)
{
    if (d < 0 || d > 9) {
        return;
    }
    uint8_t s = seg_map[d];
    int half = (h - t) / 2;         /* y offset of the middle segment */

    if (s & SA) fill_rect(x + t, y,             w - 2 * t, t, BLACK);
    if (s & SG) fill_rect(x + t, y + half,      w - 2 * t, t, BLACK);
    if (s & SD) fill_rect(x + t, y + h - t,     w - 2 * t, t, BLACK);
    if (s & SF) fill_rect(x,         y + t,        t, half - t, BLACK);
    if (s & SB) fill_rect(x + w - t, y + t,        t, half - t, BLACK);
    if (s & SE) fill_rect(x,         y + half + t, t, h - half - 2 * t, BLACK);
    if (s & SC) fill_rect(x + w - t, y + half + t, t, h - half - 2 * t, BLACK);
}

static void draw_colon(int x, int y, int size)
{
    fill_rect(x, y,            size, size, BLACK);
    fill_rect(x, y + 3 * size, size, size, BLACK);
}

static void render_clock(const struct tm *t)
{
    Paint_NewImage(s_fb, EPD_W, EPD_H, ROTATE_0, WHITE);
    EPD_Full(WHITE);

    /* Big HH:MM - digits 44x78, thickness 9 */
    const int dw = 44, dh = 78, th = 9, gap = 10, colon_w = 10;
    const int total = 4 * dw + 3 * gap + colon_w;   /* 216 px */
    int x = (EPD_W - total) / 2;
    const int y = 8;

    bool is_pm = t->tm_hour >= 12;
    int hh = t->tm_hour % 12;
    if (hh == 0) {
        hh = 12;
    }
    int mm = t->tm_min;

    draw_7seg_digit(x, y, dw, dh, th, hh / 10);  x += dw + gap;
    draw_7seg_digit(x, y, dw, dh, th, hh % 10);  x += dw + gap;
    draw_colon(x, y + dh / 2 - 2 * colon_w, colon_w);
    x += colon_w + gap;
    draw_7seg_digit(x, y, dw, dh, th, mm / 10);  x += dw + gap;
    draw_7seg_digit(x, y, dw, dh, th, mm % 10);

    /* Small DD-MM-YYYY under the clock - digits 12x20, thickness 3 */
    const int sw = 12, sh = 20, st = 3, sg = 4, dash_w = 8;
    int dd = t->tm_mday, mo = t->tm_mon + 1, yy = t->tm_year + 1900;
    int digits[8] = { dd / 10, dd % 10, mo / 10, mo % 10,
                      (yy / 1000) % 10, (yy / 100) % 10,
                      (yy / 10) % 10, yy % 10 };
    const int date_total = 8 * sw + 7 * sg + 2 * (dash_w + sg);
    int dx = (EPD_W - date_total) / 2;
    const int dy = 96;

    /* AM/PM indicator (8x16 font) in the margin to the right of the date */
    EPD_ShowString(dx + date_total + sg + 10, dy + (sh - 16) / 2,
                   is_pm ? "PM" : "AM", 16, BLACK);

    for (int i = 0; i < 8; i++) {
        draw_7seg_digit(dx, dy, sw, sh, st, digits[i]);
        dx += sw + sg;
        if (i == 1 || i == 3) {   /* dash after DD and MM */
            fill_rect(dx, dy + sh / 2 - 1, dash_w, st, BLACK);
            dx += dash_w + sg;
        }
    }
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

    int part_count = 0;
    int last_min = -1;

    while (1) {
        time_t now;
        struct tm t;
        time(&now);
        localtime_r(&now, &t);

        if (t.tm_min != last_min) {
            last_min = t.tm_min;
            render_clock(&t);

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

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
