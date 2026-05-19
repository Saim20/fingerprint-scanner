idf_component_register(SRCS "app_buttons.c" "app_oled.c" "app_fingerprint.c" "app_buzzer.c" "app_wifi.c" "app_cloud.c" "station_example_main.c"
                    PRIV_REQUIRES nvs_flash nixy4__u8g2 esp_lcd driver esp_driver_i2c esp_driver_uart esp_driver_ledc esp_driver_gpio esp_wifi esp_http_client cjson mbedtls
                    INCLUDE_DIRS ".")
