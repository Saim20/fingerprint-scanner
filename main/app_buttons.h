#pragma once

#include "esp_err.h"

/**
 * B3F-4055 — one switch pin to GPIO, other pin to GND (released=1, pressed=0).
 *
 * ESP32-C3 Super Mini:
 *   GPIO0 ──[SW] GO (run pending cloud command)
 *
 * Avoid GPIO4/7 (fingerprint UART), GPIO10 (buzzer).
 */
#define APP_BTN_PIN_GO  0

typedef void (*app_btn_cb_t)(void *ctx);

esp_err_t app_buttons_init(app_btn_cb_t go_cb, void *ctx);

/** Print GPIO level to stdout (1=released, 0=pressed). */
void app_buttons_print_levels(void);
