# CrowPanel ESP32-S3 2.13" E-Paper Clock (ESP-IDF)

Displays the current time (12-hour HH:MM with an AM/PM indicator) and
date on the Elecrow CrowPanel 2.13" e-paper HMI (122x250, SSD1680Z).
Time is synced via WiFi/SNTP. Partial refresh every minute; every 4th
update instead does a black->white double-flash full refresh
(`epd_flash_clean()`) to reset pixel dipoles before drawing, and the
controller's previous-image RAM is re-synced after every refresh so
stale digit segments don't linger between updates.

Graphics are drawn with `main/gfx.c`, a plain-C port of the
[Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library) drawing
primitives that [GxEPD2](https://github.com/ZinggJM/GxEPD2) itself is
built on for text/shape rendering (`GxEPD2_GFX : public Adafruit_GFX`)
- same GFXfont format, same `setFont()`/`setCursor()`/`print()`-style
API, same line/circle/rect algorithms, translated from the Arduino C++
originals to C so no Arduino/Adafruit_GFX/GxEPD2 dependency is needed.
It draws into the framebuffer this project already maintains
(`main/epd213.c`), so the SSD1680 panel init/refresh/ghosting-fix
sequencing is untouched. Font tables (converted from GNU FreeFont) live
in `main/fonts/` and come from Adafruit-GFX-Library (BSD license),
unmodified aside from dropping the AVR-only `PROGMEM` annotation.

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
