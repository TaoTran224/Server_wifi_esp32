idf_component_register(
    SRCS "wifi_example_main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_event esp_netif esp_wifi nvs_flash log
)
