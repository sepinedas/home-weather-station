# CrowPanel ESP32-S3 2.13" E-Paper Clock (ESP-IDF)

Displays the current time (12-hour HH:MM with an AM/PM indicator,
seven-segment style) and date on the Elecrow CrowPanel 2.13" e-paper HMI
(122x250, SSD1680Z). Time is synced via WiFi/SNTP. Partial refresh every
minute; a full refresh every 10 updates clears ghosting.

Panel rendering (SPI/GPIO driving, RAM writes, full/partial refresh) is
delegated to the
[`antunesls/crowpanel_epaper_driver_component`](https://github.com/antunesls/crowpanel_epaper_driver_component)
managed component (see `main/idf_component.yml`); `main.c` only builds
the clock face (seven-segment digits, colon, AM/PM label) into the
component's image buffer via its `Paint_*`/`EPD_Draw*`/`EPD_ShowString`
API.

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

The build fetches `crowpanel_epaper_driver_component` from the ESP
Component Registry on first configure (requires network access from the
build machine).

Tested layout: ESP-IDF v5.x. If WiFi/SNTP fails, the clock still runs
but starts from the epoch until time is synced.

### Known caveat: partial refresh on the 2.13" panel

The component's `EPD_Display()` (full refresh) does an internal
logical-to-physical coordinate remap that's specific to this panel's
portrait-native controller. `EPD_Display_Part()`, which this project
uses for the once-a-minute fast refresh, does **not** appear to do the
same remap in the component's current source — it programs the RAM
address window directly from the `sizex`/`sizey` you pass it. Called
with the full logical 250x122 frame (as this project does), that looks
like it could program a mismatched address window on this specific
display. It's possible this is harmless in practice on real hardware,
but it hasn't been verified against a physical panel here. If the
partial-refresh path renders incorrectly, set `FULL_REFRESH_EVERY` to
`1` in `main.c` to always use the full-refresh path as a fallback while
this gets sorted out upstream.
