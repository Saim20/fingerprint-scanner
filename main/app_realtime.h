#pragma once

#include <stdbool.h>

#include "app_cloud.h"
#include "esp_err.h"

typedef void (*app_realtime_command_cb_t)(const app_cloud_sync_t *cmd, void *ctx);

esp_err_t app_realtime_start(app_realtime_command_cb_t on_command, void *ctx);
bool app_realtime_is_connected(void);
void app_realtime_refresh(void);
/**
 * Block WSS sends while HTTPS runs.
 * @param tear_down_ws If true, also destroy WSS (for enroll/ack events). If false,
 *        keep the connection up (routine sync polls).
 */
void app_realtime_suspend_for_http(bool tear_down_ws);
void app_realtime_resume_after_http(void);
