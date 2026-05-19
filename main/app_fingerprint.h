#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Module stores users with ID 1..150 (ID 0 is rejected with ACK_FAIL). */
#define APP_FP_MIN_USER_ID  1
#define APP_FP_MAX_USER_ID  150
#define APP_FP_MAX_SLOTS    (APP_FP_MAX_USER_ID - APP_FP_MIN_USER_ID + 1)

/** UART1 — do not use GPIO20/21 (USB serial console). Cross: ESP TX→module RX, ESP RX←module TX. */
#define APP_FP_PIN_TX      4
#define APP_FP_PIN_RX      7
/** FPC1020A default UART rate (set in module docs / factory config). */
#define APP_FP_BAUD        19200
#define APP_FP_SWAP_TX_RX  1       /* your module: TX=GPIO7 RX=GPIO4 */

esp_err_t app_fp_init(void);
bool app_fp_is_ready(void);

/** Three-step enroll; place finger when OLED prompts. Returns ESP_OK on success. */
esp_err_t app_fp_enroll(uint16_t slot_id);

/** Single enroll step (1..3); use between UI updates. */
esp_err_t app_fp_enroll_step(uint16_t slot_id, int step);

/** 1:N search; sets *matched and *out_id on success. */
esp_err_t app_fp_search(uint16_t *out_id, bool *matched);

/** 1:1 verify against one stored template (use right after enroll). */
esp_err_t app_fp_identify(uint16_t slot_id);

esp_err_t app_fp_delete(uint16_t slot_id);
esp_err_t app_fp_clear_all(void);

/** Stored template count (module report). */
esp_err_t app_fp_get_user_count(uint8_t *count);

/** Last matched / enrolled user id from module (after search/enroll). */
uint16_t app_fp_last_user_id(void);

/** Last module ACK byte (Q3) from a failed command; 0 if last cmd succeeded. */
uint8_t app_fp_last_ack(void);

/** Short description of a module ACK byte for user messages. */
const char *app_fp_ack_str(uint8_t ack);

/** Try to capture one image (0x23). ESP_OK if sensor sees a finger. */
esp_err_t app_fp_test_sensor(void);

/** Last known template count (updated on enroll/delete/clear/count query). */
uint8_t app_fp_stored_count(void);
