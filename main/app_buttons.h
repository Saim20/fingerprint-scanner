#pragma once

#include "esp_err.h"

/**
 * B3F-4055 — one switch pin to GPIO, other pin to GND (released=1, pressed=0).
 *
 * ESP32-C3 Super Mini (pins on the board):
 *   GPIO0 ──[SW] enroll
 *   GPIO1 ──[SW] scan
 *   GPIO2 ──[SW] auto
 *   GPIO3 ──[SW] delete
 *   (all switches share GND)
 *
 * GPIO2 is a strapping pin — avoid holding it low at reset.
 * Avoid GPIO4/7 (fingerprint UART), GPIO8/9 (LED), GPIO10 (buzzer).
 */
#define APP_BTN_PIN_ENROLL      0
#define APP_BTN_PIN_SCAN        1
#define APP_BTN_PIN_AUTO        2
#define APP_BTN_PIN_DELETE      3

typedef enum {
    APP_BTN_ENROLL = 0,
    APP_BTN_SCAN,
    APP_BTN_AUTO,
    APP_BTN_DELETE,
    APP_BTN_COUNT,
} app_btn_id_t;

/** long_press is true only for DELETE held ~1.5s (clear all templates). */
typedef void (*app_btn_cb_t)(app_btn_id_t btn, bool long_press, void *ctx);

esp_err_t app_buttons_init(app_btn_cb_t cb, void *ctx);

/** Print GPIO levels to stdout (1=released, 0=pressed). */
void app_buttons_print_levels(void);
