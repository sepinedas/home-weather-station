# CrowPanel ESP32-S3 2.13" E-Paper Clock (ESP-IDF)

Displays the current time (12-hour HH:MM with an AM/PM badge) and date
on the Elecrow CrowPanel 2.13" e-paper HMI (122x250, SSD1680Z). Time is
synced via WiFi/SNTP. Partial refresh every minute; a full refresh
every 10 updates clears ghosting, and the controller's previous-image
RAM is re-synced after every refresh so stale glyphs don't linger
between updates.

The clock face is rendered with [LVGL](https://lvgl.io) (`main/lvgl_ui.c`)
through a monochrome display driver, giving proper anti-aliased-then-
dithered fonts (Montserrat), a styled AM/PM badge, and a divider, instead
of hand-drawn seven-segment blocks. `main/epd213.c` still owns the raw
SSD1680 SPI/register sequence and power management; LVGL only supplies
the framebuffer via `fb_set_pixel()`. LVGL is pulled in as a managed
component (`main/idf_component.yml`, `lvgl/lvgl ~8.3.11`) and configured
entirely through `sdkconfig.defaults` (no `lv_conf.h` needed) — `idf.py`
needs network access on first build to fetch it from the component
registry.

## Pinout (fixed on this board)
| Signal | GPIO |
|--------|------|
| Panel power enable | 7 |
| BUSY | 9 |
| RES  | 10 |
| MOSI | 11 |
| SCK  | 12 |
| DC   | 13 |
| CS   | 14 |

## Build & flash
```bash
idf.py set-target esp32s3
idf.py menuconfig      # E-paper Clock Configuration -> WiFi SSID/password, timezone
idf.py flash monitor
```

Tested layout: ESP-IDF v5.x. If WiFi/SNTP fails, the clock still runs
but starts from the epoch until time is synced.
