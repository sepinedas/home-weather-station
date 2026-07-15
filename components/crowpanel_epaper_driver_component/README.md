# Vendored copy of crowpanel_epaper_driver_component

This is a locally vendored copy of
[`antunesls/crowpanel_epaper_driver_component`](https://github.com/antunesls/crowpanel_epaper_driver_component)
(MIT licensed, see `LICENSE`), pulled from its `main` branch instead of
depending on the ESP Component Registry package.

## Why vendored instead of a managed dependency

The upstream `CMakeLists.txt` only declares `REQUIRES driver`, but on
this project's ESP-IDF version `driver/gpio.h` is only resolved via the
separate `esp_driver_gpio` component (this project's own `main`
component already needed both, see its `CMakeLists.txt`). Depending on
the managed package as-is fails with:

```
managed_components/antunesls__crowpanel_epaper_driver_component/epaper_driver.c:5:10:
fatal error: driver/gpio.h: No such file or directory
```

The only change from upstream is adding `esp_driver_gpio` to
`CMakeLists.txt`'s `REQUIRES`. Everything else (`epaper_driver.c`,
`epaper_fonts_data.c`, `include/`, `Kconfig`) is an unmodified copy.

If upstream fixes this in a future release, this vendored copy can be
dropped in favor of the managed dependency again (re-add it to
`main/idf_component.yml`).
