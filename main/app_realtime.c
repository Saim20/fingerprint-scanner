#include "app_realtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "app_cloud.h"
#include "app_wifi.h"

#define RT_TAG "app_realtime"
#define RT_TOKEN_BUF 768
#define RT_WS_BUF 4096
#define RT_HEARTBEAT_MS 25000
#define RT_TASK_STACK 10240

static esp_websocket_client_handle_t s_ws;
static bool s_connected;
static bool s_joined;
static bool s_task_started;
static int s_ref;
static int64_t s_token_expires_at;
static char s_access_token[RT_TOKEN_BUF];
static char s_channel_topic[80];
static char s_ws_uri[384];
static app_realtime_command_cb_t s_cmd_cb;
static void *s_cmd_ctx;

static void parse_string_field(cJSON *item, char *out, size_t out_len)
{
    if (item != NULL && cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(out, item->valuestring, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

static void build_channel_topic(void)
{
    const char *id = app_cloud_device_id();
    if (id[0] == '\0') {
        s_channel_topic[0] = '\0';
        return;
    }
    snprintf(s_channel_topic, sizeof(s_channel_topic), "realtime:device-%s", id);
}

static bool token_valid(void)
{
    if (s_access_token[0] == '\0') {
        return false;
    }
    time_t now = time(NULL);
    return now > 0 && now + 120 < s_token_expires_at;
}

static esp_err_t refresh_access_token(void)
{
    int expires_in = 0;
    esp_err_t err = app_cloud_fetch_realtime_token(s_access_token, sizeof(s_access_token),
                                                   &expires_in);
    if (err != ESP_OK) {
        return err;
    }
    time_t now = time(NULL);
    if (now <= 0) {
        s_token_expires_at = 0;
    } else {
        s_token_expires_at = now + (expires_in > 0 ? expires_in : 3600);
    }
    ESP_LOGI(RT_TAG, "realtime token ok (expires in %ds)", expires_in);
    return ESP_OK;
}

static bool build_ws_uri(void)
{
    const char *base = app_cloud_base_url();
    const char *pub = app_cloud_publishable_key();
    if (base[0] == '\0' || pub[0] == '\0') {
        return false;
    }

    char host[128];
    host[0] = '\0';
    if (strncmp(base, "https://", 8) == 0) {
        strncpy(host, base + 8, sizeof(host) - 1);
    } else if (strncmp(base, "http://", 7) == 0) {
        strncpy(host, base + 7, sizeof(host) - 1);
    } else {
        return false;
    }
    char *slash = strchr(host, '/');
    if (slash != NULL) {
        *slash = '\0';
    }

    snprintf(s_ws_uri, sizeof(s_ws_uri),
             "wss://%s/realtime/v1/websocket?apikey=%s&vsn=1.0.0", host, pub);
    return true;
}

static esp_err_t ws_send_json(const char *json)
{
    if (s_ws == NULL || !s_connected || json == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int n = esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(3000));
    return n >= 0 ? ESP_OK : ESP_FAIL;
}

static void send_heartbeat(void)
{
    char msg[96];
    snprintf(msg, sizeof(msg),
             "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"%d\"}", ++s_ref);
    (void)ws_send_json(msg);
}

static void send_channel_join(void)
{
    if (s_channel_topic[0] == '\0' || s_access_token[0] == '\0') {
        return;
    }

    const char *id = app_cloud_device_id();
    char filter[64];
    snprintf(filter, sizeof(filter), "id=eq.%s", id);

    char payload[RT_WS_BUF];
    int n = snprintf(payload, sizeof(payload),
                     "{"
                     "\"topic\":\"%s\","
                     "\"event\":\"phx_join\","
                     "\"payload\":{"
                     "\"config\":{"
                     "\"broadcast\":{\"self\":false},"
                     "\"presence\":{\"key\":\"\"},"
                     "\"postgres_changes\":[{"
                     "\"event\":\"UPDATE\","
                     "\"schema\":\"public\","
                     "\"table\":\"devices\","
                     "\"filter\":\"%s\""
                     "}]"
                     "},"
                     "\"access_token\":\"%s\""
                     "},"
                     "\"ref\":\"%d\""
                     "}",
                     s_channel_topic, filter, s_access_token, ++s_ref);
    if (n <= 0 || n >= (int)sizeof(payload)) {
        ESP_LOGW(RT_TAG, "join payload too large");
        return;
    }
    ESP_LOGI(RT_TAG, "join %s", s_channel_topic);
    (void)ws_send_json(payload);
}

static void merge_sync_fields(app_cloud_sync_t *cmd)
{
    const app_cloud_sync_t *sync = app_cloud_last_sync();
    if (sync == NULL || !sync->valid || sync->command_seq != cmd->command_seq) {
        return;
    }
    if (sync->person_display_name[0] != '\0') {
        strncpy(cmd->person_display_name, sync->person_display_name,
                sizeof(cmd->person_display_name) - 1);
    }
    if (sync->person_external_id[0] != '\0') {
        strncpy(cmd->person_external_id, sync->person_external_id,
                sizeof(cmd->person_external_id) - 1);
    }
    if (cmd->desired_fp_slot == 0 && sync->desired_fp_slot > 0) {
        cmd->desired_fp_slot = sync->desired_fp_slot;
    }
}

static void handle_device_record(cJSON *record)
{
    if (record == NULL) {
        return;
    }

    cJSON *bg = cJSON_GetObjectItem(record, "background_scan");
    if (cJSON_IsBool(bg)) {
        (void)app_cloud_set_background_scan(cJSON_IsTrue(bg));
    }

    if (s_cmd_cb == NULL) {
        return;
    }

    app_cloud_sync_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cJSON *seq = cJSON_GetObjectItem(record, "command_seq");
    if (cJSON_IsNumber(seq)) {
        cmd.command_seq = (int64_t)seq->valuedouble;
    }
    parse_string_field(cJSON_GetObjectItem(record, "desired_mode"), cmd.desired_mode,
                       sizeof(cmd.desired_mode));
    parse_string_field(cJSON_GetObjectItem(record, "desired_person_id"), cmd.desired_person_id,
                       sizeof(cmd.desired_person_id));

    cJSON *slot = cJSON_GetObjectItem(record, "desired_fp_slot");
    if (cJSON_IsNumber(slot)) {
        cmd.desired_fp_slot = (uint16_t)slot->valuedouble;
    }

    cmd.valid = cmd.desired_mode[0] != '\0' && cmd.command_seq > 0;
    if (!cmd.valid) {
        return;
    }

    merge_sync_fields(&cmd);

    ESP_LOGI(RT_TAG, "push seq=%lld mode=%s slot=%u",
             (long long)cmd.command_seq, cmd.desired_mode, (unsigned)cmd.desired_fp_slot);
    s_cmd_cb(&cmd, s_cmd_ctx);
}

static void handle_ws_message(const char *data, int len)
{
    if (data == NULL || len <= 0) {
        return;
    }

    char *buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, data, (size_t)len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root == NULL) {
        return;
    }

    cJSON *event = cJSON_GetObjectItem(root, "event");
    cJSON *payload = cJSON_GetObjectItem(root, "payload");

    if (cJSON_IsString(event) && event->valuestring != NULL) {
        if (strcmp(event->valuestring, "phx_reply") == 0 && payload != NULL) {
            cJSON *status = cJSON_GetObjectItem(payload, "status");
            if (cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0) {
                s_joined = true;
                ESP_LOGI(RT_TAG, "channel joined");
            } else {
                cJSON *resp = cJSON_GetObjectItem(payload, "response");
                ESP_LOGW(RT_TAG, "join failed status=%s",
                         cJSON_IsString(status) ? status->valuestring : "?");
                if (resp != NULL) {
                    char *txt = cJSON_PrintUnformatted(resp);
                    if (txt != NULL) {
                        ESP_LOGW(RT_TAG, "response=%s", txt);
                        free(txt);
                    }
                }
            }
        } else if (strcmp(event->valuestring, "postgres_changes") == 0 && payload != NULL) {
            cJSON *data_obj = cJSON_GetObjectItem(payload, "data");
            cJSON *record = data_obj != NULL ? cJSON_GetObjectItem(data_obj, "record")
                                             : cJSON_GetObjectItem(payload, "record");
            if (record == NULL) {
                record = payload;
            }
            handle_device_record(record);
        }
    }

    cJSON_Delete(root);
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                             void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        s_joined = false;
        ESP_LOGI(RT_TAG, "websocket connected");
        send_channel_join();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        s_joined = false;
        ESP_LOGW(RT_TAG, "websocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data != NULL && data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->data_len > 0) {
            handle_ws_message(data->data_ptr, data->data_len);
        }
        break;
    default:
        break;
    }
}

static void ws_stop(void)
{
    if (s_ws != NULL) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }
    s_connected = false;
    s_joined = false;
}

static esp_err_t ws_start(void)
{
    if (!build_ws_uri() || s_channel_topic[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    ws_stop();

    esp_websocket_client_config_t cfg = {
        .uri = s_ws_uri,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = RT_WS_BUF,
        .task_stack = 6144,
    };

    s_ws = esp_websocket_client_init(&cfg);
    if (s_ws == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    return esp_websocket_client_start(s_ws);
}

static void realtime_worker_task(void *arg)
{
    (void)arg;
    int64_t last_hb_ms = 0;
    int disconnect_ticks = 0;

    for (;;) {
        if (!app_cloud_is_configured() || !app_wifi_is_connected() ||
            app_cloud_device_id()[0] == '\0') {
            ws_stop();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        build_channel_topic();

        if (!token_valid()) {
            ws_stop();
            if (refresh_access_token() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        if (s_ws == NULL) {
            if (ws_start() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        if (s_connected && s_joined) {
            int64_t now_ms = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (now_ms - last_hb_ms >= RT_HEARTBEAT_MS) {
                send_heartbeat();
                last_hb_ms = now_ms;
            }
        }

        if (s_ws != NULL && !s_connected) {
            disconnect_ticks++;
            if (disconnect_ticks > 30) {
                disconnect_ticks = 0;
                ws_stop();
            }
        } else {
            disconnect_ticks = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t app_realtime_start(app_realtime_command_cb_t on_command, void *ctx)
{
    if (s_task_started) {
        return ESP_OK;
    }
    if (on_command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cmd_cb = on_command;
    s_cmd_ctx = ctx;
    build_channel_topic();

    BaseType_t ok = xTaskCreate(realtime_worker_task, "realtime", RT_TASK_STACK, NULL, 4, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_task_started = true;
    ESP_LOGI(RT_TAG, "realtime worker started");
    return ESP_OK;
}

bool app_realtime_is_connected(void)
{
    return s_connected && s_joined;
}

void app_realtime_refresh(void)
{
    build_channel_topic();
    s_access_token[0] = '\0';
    s_token_expires_at = 0;
    ws_stop();
}
