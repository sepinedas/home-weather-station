idf_component_register(
    SRCS "main.c" "epd213.c" "gfx.c" "sevenseg.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver nvs_flash esp_wifi esp_event esp_netif lwip esp_timer esp_driver_gpio
)
