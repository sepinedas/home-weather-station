/*
 * CrowPanel ESP32-S3 2.13" e-paper clock (ESP-IDF)
 *
 * - Connects to WiFi and syncs time via SNTP
 * - Renders HH:MM, an AM/PM badge, and the date via LVGL (main/lvgl_ui.c)
 * - Partial refresh every minute, full refresh every N partials
 *   to clean up ghosting
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

#include "epd213.h"
#include "lvgl_ui.h"

static const char *TAG = "epaper_clock";

#define FULL_REFRESH_EVERY  10      /* partial refreshes between full ones */

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT  BIT0

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

    /* Bring up the panel: power, init, white it out once */
    epd_power_on();
    epd_init();
    epd_fill(EPD_COLOR_WHITE);
    epd_update_full();
    epd_clear_prev_ram();

    lvgl_ui_init();

    int part_count = 0;
    int last_min = -1;

    while (1) {
        time_t now;
        struct tm t;
        time(&now);
        localtime_r(&now, &t);

        if (t.tm_min != last_min) {
            last_min = t.tm_min;
            const uint8_t *fb = lvgl_ui_render(&t);

            epd_init();                    /* wake from deep sleep */
            epd_write_image(fb);

            if (part_count == 0) {
                epd_update_full();
            } else {
                epd_update_partial();
            }
            /* Sync the controller's "previous image" RAM to what's actually
             * on screen now, so the next partial refresh diffs against the
             * real panel state instead of a stale frame - otherwise old
             * segments never fully clear and ghost behind the new digits. */
            epd_write_prev_image(fb);
            part_count = (part_count + 1) % FULL_REFRESH_EVERY;

            epd_sleep();
            ESP_LOGI(TAG, "displayed %02d:%02d", t.tm_hour, t.tm_min);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
