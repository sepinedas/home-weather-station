/*
 * CrowPanel ESP32-S3 2.13" e-paper clock (ESP-IDF)
 *
 * - Connects to WiFi and syncs time via SNTP
 * - Draws HH:MM as seven-segment digits (see main/sevenseg.h), AM/PM and
 *   the date with real proportional fonts via gfx.c, a C port of the
 *   Adafruit_GFX drawing API GxEPD2 itself is built on (see main/gfx.h)
 * - Partial refresh every minute, full refresh every N partials
 *   to clean up ghosting
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "epd213.h"
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "gfx.h"
#include "sevenseg.h"

static const char *TAG = "epaper_clock";

#define FULL_REFRESH_EVERY 4 /* partial refreshes between full ones */

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0

static uint8_t s_fb[EPD_BUF_SIZE];
static gfx_t s_gfx;

/* ------------------------------------------------------------------ */
/* WiFi + SNTP                                                         */
/* ------------------------------------------------------------------ */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
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

static void wifi_start(void) {
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

  wifi_config_t sta = {0};
  strlcpy((char *)sta.sta.ssid, CONFIG_CLOCK_WIFI_SSID, sizeof(sta.sta.ssid));
  strlcpy((char *)sta.sta.password, CONFIG_CLOCK_WIFI_PASSWORD,
          sizeof(sta.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
  ESP_ERROR_CHECK(esp_wifi_start());
}

static bool sync_time(void) {
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  /* Wait until the year looks sane */
  for (int i = 0; i < 30; i++) {
    time_t now = 0;
    struct tm tm_now = {0};
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

/* Draws str with the given font, horizontally centered on cx, baseline at
 * baseline_y - the same "measure, then draw at a computed cursor" pattern
 * GxEPD2 examples use via getTextBounds()/setCursor()/print(). */
static void gfx_print_centered(gfx_t *g, const char *str, const GFXfont *font,
                               int16_t cx, int16_t baseline_y) {
  gfx_set_font(g, font);
  int16_t x1, y1;
  uint16_t w, h;
  gfx_get_text_bounds(g, str, 0, 0, &x1, &y1, &w, &h);
  (void)y1;
  (void)h;
  gfx_set_cursor(g, cx - w / 2 - x1, baseline_y);
  gfx_print(g, str, EPD_COLOR_BLACK);
}

static void render_clock(const struct tm *t) {
  fb_clear(s_fb, EPD_COLOR_WHITE);

  bool is_pm = t->tm_hour >= 12;
  int hh = t->tm_hour % 12;
  if (hh == 0) {
    hh = 12;
  }

  /* struct tm fields are plain int, so GCC can't otherwise prove these
   * fit the buffers below (-Werror=format-truncation) - mod them into
   * their actual valid ranges so it can. */
  int mn = t->tm_min % 100;
  int dd = t->tm_mday % 100;
  int mo = (t->tm_mon + 1) % 100;
  int yy = (t->tm_year + 1900) % 10000;

  char date_str[18];
  snprintf(date_str, sizeof(date_str), "%02d-%02d-%04d", dd, mo, yy);
  const char *ampm_str = is_pm ? "PM" : "AM";

  /* Hour/minutes as seven-segment digits, AM/PM as text right after them -
   * one centered group near the top of the panel. */
  const int16_t digit_w = 26;
  const int16_t digit_h = 46;
  const int16_t digit_thickness = 6;
  const int16_t digit_gap = 4;   /* between the two digits of a field */
  const int16_t colon_gap = 6;   /* field <-> colon spacing */
  const int16_t colon_size = 8;
  const int16_t digit_y = 8;
  const int16_t ampm_gap = 8;

  bool show_hh_tens = hh >= 10;
  int hh_tens = hh / 10;
  int hh_ones = hh % 10;
  int mn_tens = mn / 10;
  int mn_ones = mn % 10;

  int16_t hour_field_w =
      show_hh_tens ? (2 * digit_w + digit_gap) : digit_w;
  int16_t minute_field_w = 2 * digit_w + digit_gap;
  int16_t clock_w =
      hour_field_w + colon_gap + colon_size + colon_gap + minute_field_w;

  int16_t ax1, ay1;
  uint16_t aw, ah;
  gfx_set_font(&s_gfx, &FreeSansBold12pt7b);
  gfx_get_text_bounds(&s_gfx, ampm_str, 0, 0, &ax1, &ay1, &aw, &ah);
  (void)ay1;
  (void)ah;

  int16_t start_x = EPD_WIDTH / 2 - (clock_w + ampm_gap + aw) / 2;

  int16_t x = start_x;
  if (show_hh_tens) {
    sevenseg_draw_digit(&s_gfx, x, digit_y, digit_w, digit_h,
                         digit_thickness, hh_tens, EPD_COLOR_BLACK);
    x += digit_w + digit_gap;
  }
  sevenseg_draw_digit(&s_gfx, x, digit_y, digit_w, digit_h, digit_thickness,
                       hh_ones, EPD_COLOR_BLACK);
  x += digit_w + colon_gap;

  sevenseg_draw_colon(&s_gfx, x + colon_size / 2, digit_y, digit_h,
                       colon_size, EPD_COLOR_BLACK);
  x += colon_size + colon_gap;

  sevenseg_draw_digit(&s_gfx, x, digit_y, digit_w, digit_h, digit_thickness,
                       mn_tens, EPD_COLOR_BLACK);
  x += digit_w + digit_gap;
  sevenseg_draw_digit(&s_gfx, x, digit_y, digit_w, digit_h, digit_thickness,
                       mn_ones, EPD_COLOR_BLACK);
  x += digit_w;

  gfx_set_font(&s_gfx, &FreeSansBold12pt7b);
  gfx_set_cursor(&s_gfx, x + ampm_gap - ax1, digit_y + digit_h - 4);
  gfx_print(&s_gfx, ampm_str, EPD_COLOR_BLACK);

  /* Date alone, centered lower - outside the time band */
  gfx_print_centered(&s_gfx, date_str, &FreeSans9pt7b, EPD_WIDTH / 2, 100);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

void app_main(void) {
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
  xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                      pdMS_TO_TICKS(20000));

  if (!sync_time()) {
    ESP_LOGW(TAG, "SNTP failed, clock will start from epoch");
  }

  gfx_init(&s_gfx, s_fb, EPD_WIDTH, EPD_HEIGHT);

  /* Bring up the panel: power, init, white it out once */
  epd_power_on();
  epd_init();
  epd_fill(EPD_COLOR_WHITE);
  epd_update_full();
  epd_clear_prev_ram();

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

      epd_init(); /* wake from deep sleep */

      if (part_count == 0) {
        /* Deep-clean the panel before drawing so ghosting from the
         * last FULL_REFRESH_EVERY partial refreshes doesn't carry
         * over into the next batch. */
        epd_flash_clean();
        epd_write_image(s_fb);
        epd_update_full();
      } else {
        fb_clear_R26H();
        epd_write_image(s_fb);
        epd_update_partial();
      }
      part_count = (part_count + 1) % FULL_REFRESH_EVERY;

      epd_sleep();
      ESP_LOGI(TAG, "displayed %02d:%02d", t.tm_hour, t.tm_min);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
