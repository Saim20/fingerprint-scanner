#include "app_cloud.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "app_wifi.h"

#define CLOUD_TAG "app_cloud"
#define NVS_NS "cloud"
#define NVS_KEY_API "api_key"
#define NVS_KEY_URL "url"
#define NVS_KEY_CMD_SEQ "cmd_seq"

#define HTTP_BUF_SIZE 1024
#define POLL_MS 3000

static char s_api_key[128];
static char s_base_url[160];
static int64_t s_last_command_seq;
static bool s_configured;
static volatile bool s_busy;
static app_cloud_command_cb_t s_cmd_cb;
static void *s_cmd_ctx;
static bool s_task_started;

static esp_err_t nvs_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(s_api_key);
    err = nvs_get_str(h, NVS_KEY_API, s_api_key, &len);
    if (err == ESP_OK) {
        len = sizeof(s_base_url);
        if (nvs_get_str(h, NVS_KEY_URL, s_base_url, &len) != ESP_OK) {
            s_base_url[0] = '\0';
        }
        int64_t seq = 0;
        if (nvs_get_i64(h, NVS_KEY_CMD_SEQ, &seq) == ESP_OK) {
            s_last_command_seq = seq;
        }
        s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0';
    }
    nvs_close(h);
    return err;
}

static esp_err_t nvs_save_str(const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t nvs_save_cmd_seq(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i64(h, NVS_KEY_CMD_SEQ, s_last_command_seq);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static const char *effective_url(void)
{
    if (s_base_url[0] != '\0') {
        return s_base_url;
    }
#ifdef CONFIG_CLOUD_SUPABASE_URL
    if (CONFIG_CLOUD_SUPABASE_URL[0] != '\0') {
        return CONFIG_CLOUD_SUPABASE_URL;
    }
#endif
    return "";
}

static void build_function_url(char *out, size_t out_len, const char *action)
{
    const char *base = effective_url();
    snprintf(out, out_len, "%s/functions/v1/device-api?action=%s", base, action);
}

static esp_err_t http_post_json(const char *url, const char *body, char *resp, size_t resp_len)
{
    if (!s_configured || s_api_key[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    char auth[192];
    snprintf(auth, sizeof(auth), "Bearer %s", s_api_key);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (body != NULL) {
        esp_http_client_set_post_field(client, body, (int)strlen(body));
    }

    esp_err_t err = esp_http_client_open(client, body ? (int)strlen(body) : 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    if (body == NULL) {
        esp_http_client_write(client, "", 0);
    }

    int status = esp_http_client_fetch_headers(client);
    (void)status;

    int total = 0;
    if (resp != NULL && resp_len > 1) {
        resp[0] = '\0';
        while (total < (int)resp_len - 1) {
            int r = esp_http_client_read(client, resp + total, (int)resp_len - 1 - total);
            if (r <= 0) {
                break;
            }
            total += r;
        }
        resp[total] = '\0';
    }

    int code = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (code < 200 || code >= 300) {
        ESP_LOGW(CLOUD_TAG, "HTTP %d url=%s body=%s", code, url, resp ? resp : "");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void parse_sync(const char *json, app_cloud_sync_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return;
    }

    cJSON *seq = cJSON_GetObjectItem(root, "command_seq");
    if (cJSON_IsNumber(seq)) {
        out->command_seq = (int64_t)seq->valuedouble;
    }

    cJSON *mode = cJSON_GetObjectItem(root, "desired_mode");
    if (cJSON_IsString(mode) && mode->valuestring) {
        strncpy(out->desired_mode, mode->valuestring, sizeof(out->desired_mode) - 1);
    }

    cJSON *pid = cJSON_GetObjectItem(root, "desired_person_id");
    if (cJSON_IsString(pid) && pid->valuestring) {
        strncpy(out->desired_person_id, pid->valuestring, sizeof(out->desired_person_id) - 1);
    }

    cJSON *slot = cJSON_GetObjectItem(root, "desired_fp_slot");
    if (cJSON_IsNumber(slot)) {
        out->desired_fp_slot = (uint16_t)slot->valuedouble;
    }

    cJSON *name = cJSON_GetObjectItem(root, "person_display_name");
    if (cJSON_IsString(name) && name->valuestring) {
        strncpy(out->person_display_name, name->valuestring, sizeof(out->person_display_name) - 1);
    }

    out->valid = out->desired_mode[0] != '\0';
    cJSON_Delete(root);
}

static esp_err_t do_sync(app_cloud_sync_t *out)
{
    char url[256];
    build_function_url(url, sizeof(url), "sync");

    char resp[HTTP_BUF_SIZE];
    esp_err_t err = http_post_json(url, "{}", resp, sizeof(resp));
    if (err != ESP_OK) {
        return err;
    }
    parse_sync(resp, out);
    return out->valid ? ESP_OK : ESP_FAIL;
}

esp_err_t app_cloud_init(void)
{
    s_api_key[0] = '\0';
    s_base_url[0] = '\0';
    s_last_command_seq = 0;
    s_configured = false;

#ifdef CONFIG_CLOUD_DEVICE_API_KEY
    if (CONFIG_CLOUD_DEVICE_API_KEY[0] != '\0') {
        strncpy(s_api_key, CONFIG_CLOUD_DEVICE_API_KEY, sizeof(s_api_key) - 1);
    }
#endif

    esp_err_t err = nvs_load();
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0';
        return ESP_OK;
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(CLOUD_TAG, "nvs load: %s", esp_err_to_name(err));
    }

    if (s_api_key[0] == '\0') {
#ifdef CONFIG_CLOUD_DEVICE_API_KEY
        if (CONFIG_CLOUD_DEVICE_API_KEY[0] != '\0') {
            strncpy(s_api_key, CONFIG_CLOUD_DEVICE_API_KEY, sizeof(s_api_key) - 1);
        }
#endif
    }

    s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0';
    if (s_configured) {
        ESP_LOGI(CLOUD_TAG, "cloud configured, url=%s", effective_url());
    } else {
        ESP_LOGW(CLOUD_TAG, "cloud not configured (provision API key + URL)");
    }
    return ESP_OK;
}

bool app_cloud_is_configured(void)
{
    return s_configured;
}

void app_cloud_set_busy(bool busy)
{
    s_busy = busy;
}

int64_t app_cloud_last_command_seq(void)
{
    return s_last_command_seq;
}

esp_err_t app_cloud_provision(const char *api_key)
{
    if (api_key == NULL || strlen(api_key) < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_save_str(NVS_KEY_API, api_key);
    if (err != ESP_OK) {
        return err;
    }
    strncpy(s_api_key, api_key, sizeof(s_api_key) - 1);
    s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0';
    return ESP_OK;
}

esp_err_t app_cloud_set_url(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_save_str(NVS_KEY_URL, url);
    if (err != ESP_OK) {
        return err;
    }
    strncpy(s_base_url, url, sizeof(s_base_url) - 1);
    s_configured = s_api_key[0] != '\0';
    return ESP_OK;
}

esp_err_t app_cloud_report_scan(uint16_t fp_slot)
{
    char url[256];
    build_function_url(url, sizeof(url), "event");

    char body[128];
    snprintf(body, sizeof(body), "{\"type\":\"scan\",\"fp_slot\":%u}", (unsigned)fp_slot);
    return http_post_json(url, body, NULL, 0);
}

esp_err_t app_cloud_report_enroll_done(uint16_t fp_slot, const char *person_id)
{
    if (person_id == NULL || person_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char url[256];
    build_function_url(url, sizeof(url), "event");

    char body[192];
    snprintf(body, sizeof(body),
             "{\"type\":\"enroll_done\",\"fp_slot\":%u,\"person_id\":\"%s\"}",
             (unsigned)fp_slot, person_id);
    return http_post_json(url, body, NULL, 0);
}

esp_err_t app_cloud_ack(int64_t command_seq)
{
    char url[256];
    build_function_url(url, sizeof(url), "event");

    char body[96];
    snprintf(body, sizeof(body), "{\"type\":\"ack\",\"command_seq\":%lld}", (long long)command_seq);
    esp_err_t err = http_post_json(url, body, NULL, 0);
    if (err == ESP_OK) {
        s_last_command_seq = command_seq;
        nvs_save_cmd_seq();
    }
    return err;
}

static void cloud_worker_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));

    for (;;) {
        if (!s_configured || !app_wifi_is_connected() || s_busy || s_cmd_cb == NULL) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }

        app_cloud_sync_t sync;
        if (do_sync(&sync) == ESP_OK && sync.command_seq > s_last_command_seq) {
            ESP_LOGI(CLOUD_TAG, "new command seq=%lld mode=%s",
                     (long long)sync.command_seq, sync.desired_mode);
            s_cmd_cb(&sync, s_cmd_ctx);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t app_cloud_start_task(app_cloud_command_cb_t on_command, void *ctx)
{
    s_cmd_cb = on_command;
    s_cmd_ctx = ctx;
    if (!s_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_task_started) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(cloud_worker_task, "cloud", 6144, NULL, 4, NULL);
    if (ok == pdPASS) {
        s_task_started = true;
        return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}
