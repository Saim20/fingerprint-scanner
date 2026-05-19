#pragma once

#include "esp_err.h"

/**
 * External I2C SH1106 OLED (1.3" 128x64).
 * Default: SDA=GPIO5, SCL=GPIO6, VCC=3V3, GND=GND.
 *
 * Many boards label the address as 0x78 / 0x7A on silkscreen (8-bit write
 * address). Jumper on 0x78 → I2C 7-bit address 0x3C (what we probe).
 * 4-pin header is often: GND, VCC, SCL, SDA (left→right, text upright).
 */
#define APP_OLED_ENABLE         1

#define APP_OLED_PIN_SDA        5
#define APP_OLED_PIN_SCL        6

esp_err_t app_oled_init(void);
bool app_oled_is_ready(void);
void app_oled_show_lines(const char *line1, const char *line2, const char *line3, const char *line4);

/** Print status / run I2C probe on candidate pins (serial command: oled). */
void app_oled_diag(void);
