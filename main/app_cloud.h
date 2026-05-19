#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_CLOUD_LABEL_MAX 48
#define APP_CLOUD_EXT_ID_MAX 32
#define APP_CLOUD_PERSON_ID_MAX 40
#define APP_CLOUD_MAX_MAPPINGS 32

typedef struct {
    uint16_t fp_slot;
    char person_id[APP_CLOUD_PERSON_ID_MAX];
    char display_name[APP_CLOUD_LABEL_MAX];
    char external_id[APP_CLOUD_EXT_ID_MAX];
} app_cloud_mapping_t;

typedef struct {
    int64_t command_seq;
    char desired_mode[16];
    char desired_person_id[APP_CLOUD_PERSON_ID_MAX];
    uint16_t desired_fp_slot;
    char person_display_name[APP_CLOUD_LABEL_MAX];
    char person_external_id[APP_CLOUD_EXT_ID_MAX];
    int64_t ack_seq;
    bool background_scan;
    bool valid;
    /* Template sync (from server) */
    uint8_t mapping_count;
    app_cloud_mapping_t mappings[APP_CLOUD_MAX_MAPPINGS];
    uint8_t unmapped_count;
    uint16_t unmapped_slots[16];
    uint8_t stale_count;
    uint16_t stale_slots[16];
} app_cloud_sync_t;

typedef void (*app_cloud_command_cb_t)(const app_cloud_sync_t *cmd, void *ctx);
typedef void (*app_cloud_sync_cb_t)(const app_cloud_sync_t *sync, void *ctx);
typedef void (*app_cloud_settings_cb_t)(void *ctx);

esp_err_t app_cloud_init(void);
esp_err_t app_cloud_start_task(app_cloud_command_cb_t on_command, void *ctx);
void app_cloud_set_sync_callback(app_cloud_sync_cb_t on_sync, void *ctx);
void app_cloud_set_settings_callback(app_cloud_settings_cb_t on_settings, void *ctx);
void app_cloud_set_busy(bool busy);
bool app_cloud_is_configured(void);
bool app_cloud_background_scan_enabled(void);
/** Returns true if the value changed. */
bool app_cloud_set_background_scan(bool enabled);
const char *app_cloud_base_url(void);
const char *app_cloud_publishable_key(void);
const app_cloud_sync_t *app_cloud_last_sync(void);
esp_err_t app_cloud_fetch_realtime_token(char *token_out, size_t token_len, int *expires_in_sec);
esp_err_t app_cloud_provision(const char *api_key);
esp_err_t app_cloud_clear_api_key(void);
esp_err_t app_cloud_set_url(const char *url);
esp_err_t app_cloud_set_device_id(const char *device_id);
const char *app_cloud_device_id(void);
esp_err_t app_cloud_report_scan(uint16_t fp_slot);
esp_err_t app_cloud_report_enroll_done(uint16_t fp_slot, const char *person_id);
esp_err_t app_cloud_report_slot_cleared(uint16_t fp_slot);
esp_err_t app_cloud_report_all_cleared(void);
esp_err_t app_cloud_request_sync(void);
esp_err_t app_cloud_ack(int64_t command_seq);
/** Immediate ack (bypasses queue) — for idle/cancel. */
esp_err_t app_cloud_ack_now(int64_t command_seq);
int64_t app_cloud_last_command_seq(void);

/** True when sync carries a command the device has not yet acked. */
bool app_cloud_has_pending(const app_cloud_sync_t *sync);

/** Label for OLED/serial: external_id, display_name, or "slot N". */
const char *app_cloud_slot_label(uint16_t fp_slot);

/** Person UUID for slot, or NULL if unmapped. */
const char *app_cloud_slot_person_id(uint16_t fp_slot);
