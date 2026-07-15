# CrowPanel ESP32-S3 2.13" E-Paper Clock (ESP-IDF)

Displays the current time (12-hour HH:MM with an AM/PM indicator,
seven-segment style) and date on the Elecrow CrowPanel 2.13" e-paper HMI
(122x250, SSD1680Z). Time is synced via WiFi/SNTP. Partial refresh every
minute; a full refresh every 10 updates clears ghosting, and the
controller's previous-image RAM is re-synced after every refresh so
stale digit segments don't linger between updates.

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
