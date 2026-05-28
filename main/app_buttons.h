#pragma once

#include "esp_err.h"

/**
 * B3F-4055 — one switch pin to GPIO, other pin to GND (released=1, pressed=0).
 *
 * ESP32-C3 Super Mini:
 *   GPIO0 ──[SW] GO (run pending cloud command)
 *   GPIO1 ──[SW] ENROLL (bare-minimum enroll test → slot 1)
 *   GPIO2–3 — spare
 *
 * GPIO2 is a strapping pin — avoid holding it low at reset.
 * Avoid GPIO4/7 (fingerprint UART), GPIO5/6 (OLED I2C), GPIO10 (buzzer).
 */
#define APP_BTN_PIN_GO          0
#define APP_BTN_PIN_ENROLL      1
#define APP_BTN_PIN_UNUSED_2    2
#define APP_BTN_PIN_UNUSED_3    3

typedef void (*app_btn_cb_t)(void *ctx);

esp_err_t app_buttons_init(app_btn_cb_t go_cb, app_btn_cb_t enroll_cb, void *ctx);

/** Print GPIO levels to stdout (1=released, 0=pressed). */
void app_buttons_print_levels(void);
