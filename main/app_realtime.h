#pragma once

#include <stdbool.h>

#include "app_cloud.h"
#include "esp_err.h"

typedef void (*app_realtime_command_cb_t)(const app_cloud_sync_t *cmd, void *ctx);

esp_err_t app_realtime_start(app_realtime_command_cb_t on_command, void *ctx);
bool app_realtime_is_connected(void);
void app_realtime_refresh(void);
