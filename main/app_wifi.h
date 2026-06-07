#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Start Wi-Fi station mode and connect using menuconfig SSID/password (non-blocking). */
esp_err_t app_wifi_init(void);

/** Drop cached Wi-Fi NVS (stale SSID/BSSID) and reconnect with menuconfig credentials. */
esp_err_t app_wifi_clear_stored_config(void);

/** Force a new connection attempt (resets retry counter). */
esp_err_t app_wifi_reconnect(void);

/** Print SSID, link state, IP, and last disconnect reason to stdout. */
void app_wifi_print_status(void);

bool app_wifi_is_connected(void);

/** Four-byte IPv4 in network order, or 0 if not connected. */
uint32_t app_wifi_get_ip(void);

/** Write dotted-quad IP into buf; returns false if not connected. */
bool app_wifi_ip_str(char *buf, size_t buf_len);

/** Block until SNTP sets a valid clock (needed before HTTPS). */
bool app_wifi_wait_time_sync(uint32_t timeout_ms);
