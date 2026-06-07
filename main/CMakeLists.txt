idf_component_register(SRCS "app_buttons.c" "app_fingerprint.c" "app_buzzer.c" "app_wifi.c" "app_realtime.c" "app_cloud.c" "station_example_main.c"
                    PRIV_REQUIRES nvs_flash esp_driver_uart esp_driver_ledc esp_driver_gpio esp_wifi esp_http_client esp_websocket_client cjson mbedtls
                    INCLUDE_DIRS ".")
