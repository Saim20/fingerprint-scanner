#pragma once

#include "esp_err.h"

/**
 * Set to 0 to skip OLED init (fingerprint + serial still work). Set to 1 when wired.
 * HW-466AB: external OLED on GPIO5 (SDA), GPIO6 (SCL) — not GPIO8/9 (LED/BOOT).
 */
#define APP_OLED_ENABLE  0

#define APP_OLED_PIN_SDA  5
#define APP_OLED_PIN_SCL  6
#define APP_OLED_I2C_ADDR_7BIT  0x3c

esp_err_t app_oled_init(void);
bool app_oled_is_ready(void);

/** Up to four lines; long strings are truncated to fit 128px width (5x7 font). Safe from WiFi event context (mutex). */
void app_oled_show_lines(const char *line1, const char *line2, const char *line3, const char *line4);
