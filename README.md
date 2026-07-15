# CrowPanel ESP32-S3 2.13" E-Paper Clock (ESP-IDF)

Displays the current time (12-hour HH:MM with an AM/PM indicator) and
date on the Elecrow CrowPanel 2.13" e-paper HMI (122x250, SSD1680Z).
Time is synced via WiFi/SNTP. Every minute triggers a refresh; see the
partial-refresh caveat below for why it currently always does a full
(flashing) refresh rather than a fast partial one.

The UI is built with [LVGL](https://lvgl.io/) (`lvgl/lvgl`, pinned to
`9.2.2`, managed via `main/idf_component.yml`): `main.c` creates three
`lv_label` widgets (time, date, AM/PM) styled with built-in Montserrat
fonts. Panel power/SPI/refresh is delegated to
[`antunesls/crowpanel_epaper_driver_component`](https://github.com/antunesls/crowpanel_epaper_driver_component),
vendored (with one small patch) under
`components/crowpanel_epaper_driver_component` — see that directory's
README for why it's vendored instead of a managed dependency.

LVGL renders into an 8-bit grayscale (`LV_COLOR_FORMAT_L8`) buffer;
a flush callback (`epd_flush_cb` in `main.c`) thresholds each pixel and
writes it into the crowpanel component's packed 1bpp image via
`Paint_SetPixel()`, which is what actually gets pushed to the panel.

We looked at `espressif/esp_lcd_ssd1681` (the more "native" ESP-IDF/LVGL
pairing, with an official LVGL demo) as the panel-driving layer instead,
but its source hardcodes a 200x200 resolution (gate count, RAM window
math, and framebuffer size are all compile-time constants for the 1.54"
SSD1681 panel) and can't drive this board's 250x122 SSD1680 panel
without forking it. Given that, LVGL is layered on top of the existing
crowpanel component instead, as described above.

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

These are set in `sdkconfig.defaults` under `CONFIG_CROWPANEL_EPAPER_*`
(component config for the 2.13" model). Change them there, or via
`idf.py menuconfig` -> **Component config -> CrowPanel E-Paper
Configuration**, if the wiring ever changes.

## Build & flash
```bash
idf.py set-target esp32s3
idf.py menuconfig      # E-paper Clock Configuration -> WiFi SSID/password, timezone
idf.py flash monitor
```

The build fetches `lvgl` from the ESP Component Registry on first
configure (requires network access from the build machine).

Tested layout: ESP-IDF v5.x. If WiFi/SNTP fails, the clock still runs
but starts from the epoch until time is synced.

### Known caveat: label layout not visually verified

The label positions/sizes (Montserrat 48 for the time, Montserrat 24
for the date and AM/PM) were sized against LVGL's documented font
metrics, not against a rendered frame or real hardware — there was no
way to preview this in the environment this was written in. If text
clips, overlaps, or sits off-center on first flash, adjust the
`lv_obj_align()` offsets and font sizes in `lvgl_init()` in `main.c`.

### Known caveat: partial refresh on the 2.13" panel

The component's `EPD_Display()` (full refresh) does an internal
logical-to-physical coordinate remap that's specific to this panel's
portrait-native controller. `EPD_Display_Part()` (the fast partial
refresh) does **not** appear to do the same remap in the component's
current source — it programs the RAM address window directly from the
`sizex`/`sizey` you pass it. Called with the full logical 250x122
frame (as this project would if it used it), that looks like it could
program a mismatched address window on this specific display.

This hasn't been verified against a physical panel, so
`FULL_REFRESH_EVERY` in `main.c` currently defaults to `1` — every
update is a full (flashing) refresh, trading the fast quiet update for
guaranteed-correct rendering. Once partial refresh has been confirmed
to render correctly on real hardware, raise `FULL_REFRESH_EVERY` (e.g.
back to `10`) to get the once-a-minute fast partial refresh with a
periodic full refresh to clear any ghosting.
