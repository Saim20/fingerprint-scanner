#pragma once

#include "esp_err.h"

/**
 * External I2C OLED (not the optional built-in 0.42" on some C3 boards).
 * Default: SDA=GPIO5, SCL=GPIO6, VCC=3V3, GND=GND.
 */
#define APP_OLED_ENABLE         1

#define APP_OLED_PIN_SDA        5
#define APP_OLED_PIN_SCL        6
#define APP_OLED_I2C_ADDR_7BIT  0x3c

/** 0 = SSD1306 (0.96" 128x64). 1 = SH1106 (1.3" 128x64). */
#define APP_OLED_USE_SH1106     0

esp_err_t app_oled_init(void);
bool app_oled_is_ready(void);
void app_oled_show_lines(const char *line1, const char *line2, const char *line3, const char *line4);

/** Print status / run I2C probe on candidate pins (serial command: oled). */
void app_oled_diag(void);
