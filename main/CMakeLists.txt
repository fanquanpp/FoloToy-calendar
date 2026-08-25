idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "calendar_logic.c"
         "app_net.c"
         "demo_calendar.c"
         "demo_setup.c"
         "demo_low_power.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_http_server esp_netif esp_timer esp_wifi nvs_flash
)
