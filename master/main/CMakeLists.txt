idf_component_register(SRCS "hello_world_main.c"
                       PRIV_REQUIRES spi_flash led_strip esp_wifi nvs_flash esp_http_server esp_netif driver esp_driver_tsens
                       INCLUDE_DIRS "")
