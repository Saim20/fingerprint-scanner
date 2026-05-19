#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/** Cloud command from Supabase device-api sync. */
typedef struct {
    int64_t command_seq;
    char desired_mode[16];
    char desired_person_id[40];
    uint16_t desired_fp_slot;
    char person_display_name[48];
    bool valid;
} app_cloud_sync_t;

typedef void (*app_cloud_command_cb_t)(const app_cloud_sync_t *cmd, void *ctx);

esp_err_t app_cloud_init(void);

/** Start background poll task (3s interval when Wi-Fi up). */
esp_err_t app_cloud_start_task(app_cloud_command_cb_t on_command, void *ctx);

/** True when worker may perform HTTP (not during fingerprint ops). */
void app_cloud_set_busy(bool busy);

bool app_cloud_is_configured(void);

/** Save API key to NVS (serial: provision KEY). */
esp_err_t app_cloud_provision(const char *api_key);

/** Optional: override Supabase project URL in NVS. */
esp_err_t app_cloud_set_url(const char *url);

esp_err_t app_cloud_report_scan(uint16_t fp_slot);
esp_err_t app_cloud_report_enroll_done(uint16_t fp_slot, const char *person_id);
esp_err_t app_cloud_ack(int64_t command_seq);

/** Last applied command sequence (NVS). */
int64_t app_cloud_last_command_seq(void);
