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
#define APP_FP_SWAP_TX_RX  0       /* your module: TX=GPIO7 RX=GPIO4 */

esp_err_t app_fp_init(void);
bool app_fp_is_ready(void);

#define APP_FP_ENROLL_MAX_TRIES 25

/** User-facing enroll steps (ENROLL1 → ENROLL2 → ENROLL3). */
#define APP_FP_ENROLL_STEPS 3

/** Optional 6-capture path (1× ENROLL1, 4× ENROLL2, 1× ENROLL3) — not used by default. */
#define APP_FP_ENROLL_CAPTURES 6

/** Pick next free module slot (1..150). Same strategy as button enroll in 0788220. */
esp_err_t app_fp_alloc_enroll_slot(uint16_t *out_slot);

/** Full enroll: three steps (ENROLL1/2/3), same as working 0788220 firmware. */
esp_err_t app_fp_enroll(uint16_t slot_id);

/** One capture (0..APP_FP_ENROLL_CAPTURES-1). */
esp_err_t app_fp_enroll_capture(uint16_t slot_id, int capture_idx);

/** Legacy 3-step API (maps to captures 0, 1, 5). */
esp_err_t app_fp_enroll_step(uint16_t slot_id, int step);

/** One ENROLL1/2/3 command per step (bare-minimum 3-scan path). */
esp_err_t app_fp_enroll_legacy_step(uint16_t slot_id, int step);

/** 1:N search; sets *matched and *out_id on success. */
esp_err_t app_fp_search(uint16_t *out_id, bool *matched);

/** After app_fp_search with *matched false: true if a finger was read but not recognized. */
bool app_fp_last_search_had_finger(void);

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

/** NVS-backed occupied slot list (fast; updated on enroll/delete/clear). */
esp_err_t app_fp_slots_list(uint16_t *out_slots, size_t max_slots, size_t *out_count);

/** FNV-1a hash of occupied slots (skip sync POST when unchanged). */
uint32_t app_fp_slots_hash(void);

bool app_fp_slot_is_marked(uint16_t slot_id);

/** Probe module (ENROLL1 ack 0x06 = slot used); does not use NVS bitmap. */
bool app_fp_slot_occupied(uint16_t slot_id);

/** Delete template in slot if present; OK if slot was already empty. */
esp_err_t app_fp_clear_slot(uint16_t slot_id);

/** Mark slot occupied in NVS after module confirms template stored. */
esp_err_t app_fp_mark_slot_enrolled(uint16_t slot_id);

/** Probe module for occupied slots; updates NVS bitmap (slow; boot/manual only). */
esp_err_t app_fp_slots_rebuild_registry(void);
