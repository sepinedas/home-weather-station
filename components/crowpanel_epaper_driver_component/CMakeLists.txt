idf_component_register(SRCS "epaper_driver.c" "epaper_fonts_data.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_gpio esp_timer log)
