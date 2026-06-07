#include "app_cloud.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "app_fingerprint.h"
#include "app_realtime.h"
#include "app_wifi.h"

#define CLOUD_TAG "app_cloud"
#define NVS_NS "cloud"
#define NVS_KEY_API "api_key"
#define NVS_KEY_URL "url"
#define NVS_KEY_CMD_SEQ "cmd_seq"
#define NVS_KEY_DEVICE_ID "device_id"

#define HTTP_BUF_SIZE 3072
#define POLL_MS 250
#define POLL_MS_FAST 80
#define POLL_MS_REALTIME 2000
#define HTTP_RETRIES 3
#define HTTP_TIMEOUT_MS 20000
/** TLS + full cert bundle needs headroom beyond HTTP buffers (see do_sync). */
#define CLOUD_TASK_STACK 18432

static char s_http_resp[HTTP_BUF_SIZE];
static char s_sync_url[256];
static char s_sync_req[2048];
static app_cloud_sync_t s_sync;

static char s_api_key[128];
static char s_base_url[160];
static char s_device_id[40];
static int64_t s_last_command_seq;
static bool s_configured;
static volatile bool s_busy;
static app_cloud_command_cb_t s_cmd_cb;
static void *s_cmd_ctx;
static app_cloud_sync_cb_t s_sync_cb;
static void *s_sync_ctx;
static app_cloud_settings_cb_t s_settings_cb;
static void *s_settings_ctx;
static bool s_task_started;
static bool s_background_scan;
static volatile bool s_sync_requested;
static QueueHandle_t s_msg_q;
static app_cloud_mapping_t s_slot_map[APP_FP_MAX_SLOTS];

typedef enum {
    CLOUD_MSG_SCAN,
    CLOUD_MSG_ENROLL_DONE,
    CLOUD_MSG_SLOT_CLEARED,
    CLOUD_MSG_ALL_CLEARED,
    CLOUD_MSG_ACK,
} cloud_msg_type_t;

typedef struct {
    cloud_msg_type_t type;
    uint16_t fp_slot;
    char person_id[40];
    int64_t command_seq;
} cloud_msg_t;

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

static const char *effective_publishable_key(void)
{
#ifdef CONFIG_CLOUD_SUPABASE_PUBLISHABLE_KEY
    if (CONFIG_CLOUD_SUPABASE_PUBLISHABLE_KEY[0] != '\0') {
        return CONFIG_CLOUD_SUPABASE_PUBLISHABLE_KEY;
    }
#endif
    return "";
}

static void trim_str(char *s)
{
    if (s == NULL || s[0] == '\0') {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    char *p = s;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
}

static void log_key_source(const char *source)
{
    size_t n = strlen(s_api_key);
    if (n >= 4) {
        ESP_LOGI(CLOUD_TAG, "device key from %s (...%s)", source, s_api_key + n - 4);
    } else if (n > 0) {
        ESP_LOGI(CLOUD_TAG, "device key from %s (len=%u)", source, (unsigned)n);
    }
}

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
        len = sizeof(s_device_id);
        if (nvs_get_str(h, NVS_KEY_DEVICE_ID, s_device_id, &len) != ESP_OK) {
            s_device_id[0] = '\0';
        }
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

static void build_function_url(char *out, size_t out_len, const char *action)
{
    snprintf(out, out_len, "%s/functions/v1/device-api?action=%s", effective_url(), action);
}

static void build_token_url(char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/functions/v1/device-token", effective_url());
}

static esp_err_t http_post_json(const char *url, const char *body, char *resp, size_t resp_len);

const char *app_cloud_base_url(void)
{
    return effective_url();
}

const char *app_cloud_publishable_key(void)
{
    return effective_publishable_key();
}

const app_cloud_sync_t *app_cloud_last_sync(void)
{
    return s_sync.valid ? &s_sync : NULL;
}

esp_err_t app_cloud_fetch_realtime_token(char *token_out, size_t token_len, int *expires_in_sec)
{
    if (token_out == NULL || token_len < 32) {
        return ESP_ERR_INVALID_ARG;
    }
    token_out[0] = '\0';

    char url[256];
    build_token_url(url, sizeof(url));

    char resp[512];
    esp_err_t err = http_post_json(url, "{}", resp, sizeof(resp));
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(resp);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *tok = cJSON_GetObjectItem(root, "access_token");
    cJSON *exp = cJSON_GetObjectItem(root, "expires_in");
    if (!cJSON_IsString(tok) || tok->valuestring == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(token_out, tok->valuestring, token_len - 1);
    token_out[token_len - 1] = '\0';
    if (expires_in_sec != NULL && cJSON_IsNumber(exp)) {
        *expires_in_sec = (int)exp->valuedouble;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t http_post_json_once(const char *url, const char *body, char *resp, size_t resp_len)
{
    const char *pub = effective_publishable_key();
    if (pub[0] == '\0') {
        ESP_LOGW(CLOUD_TAG, "missing publishable key (menuconfig CLOUD_SUPABASE_PUBLISHABLE_KEY)");
        return ESP_ERR_INVALID_STATE;
    }
    if (!app_wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (resp != NULL && resp_len > 0) {
        resp[0] = '\0';
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    char auth[192];
    snprintf(auth, sizeof(auth), "Bearer %s", s_api_key);
    esp_http_client_set_header(client, "apikey", pub);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    int blen = (int)strlen(body);
    esp_err_t err = esp_http_client_open(client, blen);
    if (err != ESP_OK) {
        ESP_LOGW(CLOUD_TAG, "HTTP open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    if (esp_http_client_write(client, body, blen) < 0) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_fetch_headers(client);

    if (resp != NULL && resp_len > 1) {
        int total = 0;
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
        if (code == 401) {
            ESP_LOGW(CLOUD_TAG,
                     "HTTP 401 invalid device key — menuconfig overrides NVS; try: provision clear");
        } else {
            ESP_LOGW(CLOUD_TAG, "HTTP %d body=%s", code, resp ? resp : "");
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t http_post_json(const char *url, const char *body, char *resp, size_t resp_len)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < HTTP_RETRIES; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(CLOUD_TAG, "HTTP retry %d/%d", attempt + 1, HTTP_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(300 * (uint32_t)attempt));
        }
        err = http_post_json_once(url, body, resp, resp_len);
        if (err == ESP_OK) {
            return ESP_OK;
        }
    }
    return err;
}

static void cloud_cache_mappings(const app_cloud_sync_t *sync)
{
    memset(s_slot_map, 0, sizeof(s_slot_map));
    for (uint8_t i = 0; i < sync->mapping_count && i < APP_CLOUD_MAX_MAPPINGS; i++) {
        const app_cloud_mapping_t *m = &sync->mappings[i];
        if (m->fp_slot < APP_FP_MIN_USER_ID || m->fp_slot > APP_FP_MAX_USER_ID) {
            continue;
        }
        size_t idx = (size_t)(m->fp_slot - APP_FP_MIN_USER_ID);
        s_slot_map[idx] = *m;
    }
}

static void parse_string_field(cJSON *obj, char *dest, size_t dest_len)
{
    if (cJSON_IsString(obj) && obj->valuestring) {
        strncpy(dest, obj->valuestring, dest_len - 1);
    }
}

static void parse_sync_mappings(cJSON *root, app_cloud_sync_t *out)
{
    cJSON *arr = cJSON_GetObjectItem(root, "mappings");
    if (!cJSON_IsArray(arr)) {
        return;
    }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (out->mapping_count >= APP_CLOUD_MAX_MAPPINGS) {
            break;
        }
        cJSON *slot = cJSON_GetObjectItem(item, "fp_slot");
        cJSON *pid = cJSON_GetObjectItem(item, "person_id");
        if (!cJSON_IsNumber(slot)) {
            continue;
        }
        app_cloud_mapping_t *m = &out->mappings[out->mapping_count];
        memset(m, 0, sizeof(*m));
        m->fp_slot = (uint16_t)slot->valuedouble;
        parse_string_field(pid, m->person_id, sizeof(m->person_id));
        parse_string_field(cJSON_GetObjectItem(item, "display_name"), m->display_name,
                           sizeof(m->display_name));
        parse_string_field(cJSON_GetObjectItem(item, "external_id"), m->external_id,
                           sizeof(m->external_id));
        out->mapping_count++;
    }
}

static void parse_slot_array(cJSON *root, const char *key, uint16_t *slots, uint8_t *count,
                             uint8_t max)
{
    cJSON *arr = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsArray(arr)) {
        return;
    }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (*count >= max) {
            break;
        }
        if (cJSON_IsNumber(item)) {
            uint16_t s = (uint16_t)item->valuedouble;
            if (s >= APP_FP_MIN_USER_ID && s <= APP_FP_MAX_USER_ID) {
                slots[(*count)++] = s;
            }
        }
    }
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

    cJSON *ack = cJSON_GetObjectItem(root, "ack_seq");
    if (cJSON_IsNumber(ack)) {
        out->ack_seq = (int64_t)ack->valuedouble;
    }

    parse_string_field(cJSON_GetObjectItem(root, "desired_mode"), out->desired_mode,
                       sizeof(out->desired_mode));
    parse_string_field(cJSON_GetObjectItem(root, "desired_person_id"), out->desired_person_id,
                       sizeof(out->desired_person_id));
    parse_string_field(cJSON_GetObjectItem(root, "person_display_name"), out->person_display_name,
                       sizeof(out->person_display_name));
    parse_string_field(cJSON_GetObjectItem(root, "person_external_id"), out->person_external_id,
                       sizeof(out->person_external_id));

    cJSON *slot = cJSON_GetObjectItem(root, "desired_fp_slot");
    if (cJSON_IsNumber(slot)) {
        out->desired_fp_slot = (uint16_t)slot->valuedouble;
    }

    parse_sync_mappings(root, out);
    parse_slot_array(root, "unmapped_slots", out->unmapped_slots, &out->unmapped_count, 16);
    parse_slot_array(root, "stale_slots", out->stale_slots, &out->stale_count, 16);

    cJSON *bg = cJSON_GetObjectItem(root, "background_scan");
    if (cJSON_IsBool(bg)) {
        out->background_scan = cJSON_IsTrue(bg);
    }

    out->valid = out->desired_mode[0] != '\0';
    cloud_cache_mappings(out);
    cJSON_Delete(root);
}

static int build_sync_body(char *body, size_t body_len)
{
    if (s_busy) {
        return -1;
    }

    uint8_t fp_count = 0;
    if (app_fp_get_user_count(&fp_count) != ESP_OK) {
        fp_count = app_fp_stored_count();
    }

    uint16_t slots[APP_FP_MAX_SLOTS];
    size_t n = 0;
    if (app_fp_slots_list(slots, APP_FP_MAX_SLOTS, &n) != ESP_OK) {
        n = 0;
    }

    if (n != fp_count) {
        ESP_LOGW(CLOUD_TAG, "slot registry %u vs module %u — reconciling before sync",
                 (unsigned)n, (unsigned)fp_count);

        uint16_t hints[APP_CLOUD_MAX_MAPPINGS];
        size_t hint_count = 0;
        if (s_sync.valid) {
            for (uint8_t i = 0; i < s_sync.mapping_count && hint_count < APP_CLOUD_MAX_MAPPINGS;
                 i++) {
                hints[hint_count++] = s_sync.mappings[i].fp_slot;
            }
        }

        if (app_fp_slots_reconcile_with_hints(fp_count, hints, hint_count) == ESP_OK) {
            n = 0;
            (void)app_fp_slots_list(slots, APP_FP_MAX_SLOTS, &n);
            (void)app_fp_get_user_count(&fp_count);
        }
    }

    uint32_t hash = app_fp_slots_hash();

    int off = snprintf(body, body_len,
                       "{\"fp_count\":%u,\"slots_hash\":\"%08lx\",\"fp_slots\":[",
                       (unsigned)fp_count, (unsigned long)hash);
    if (off < 0 || off >= (int)body_len) {
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        int w = snprintf(body + off, body_len - (size_t)off, "%s%u", i ? "," : "",
                         (unsigned)slots[i]);
        if (w < 0 || off + w >= (int)body_len) {
            return -1;
        }
        off += w;
    }

    int tail = snprintf(body + off, body_len - (size_t)off, "]}");
    if (tail < 0 || off + tail >= (int)body_len) {
        return -1;
    }
    return off + tail;
}

static esp_err_t do_sync(app_cloud_sync_t *out)
{
    build_function_url(s_sync_url, sizeof(s_sync_url), "sync");

    if (build_sync_body(s_sync_req, sizeof(s_sync_req)) < 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = http_post_json(s_sync_url, s_sync_req, s_http_resp, sizeof(s_http_resp));
    if (err != ESP_OK) {
        return err;
    }
    parse_sync(s_http_resp, out);
    if (!out->valid) {
        ESP_LOGW(CLOUD_TAG, "sync parse failed body=%.120s", s_http_resp);
        return ESP_FAIL;
    }

    (void)app_cloud_set_background_scan(out->background_scan);

    if (out->ack_seq > s_last_command_seq) {
        s_last_command_seq = out->ack_seq;
        nvs_save_cmd_seq();
    }
    if (s_last_command_seq > out->command_seq) {
        ESP_LOGW(CLOUD_TAG, "local seq %lld ahead of server %lld — clamping",
                 (long long)s_last_command_seq, (long long)out->command_seq);
        s_last_command_seq = out->command_seq;
        nvs_save_cmd_seq();
    }

    return ESP_OK;
}

static esp_err_t post_event(const char *body)
{
    build_function_url(s_sync_url, sizeof(s_sync_url), "event");
    return http_post_json(s_sync_url, body, NULL, 0);
}

static esp_err_t cloud_post_scan(uint16_t fp_slot)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"type\":\"scan\",\"fp_slot\":%u}", (unsigned)fp_slot);
    return post_event(body);
}

static esp_err_t cloud_post_enroll_done(uint16_t fp_slot, const char *person_id)
{
    char body[192];
    snprintf(body, sizeof(body),
             "{\"type\":\"enroll_done\",\"fp_slot\":%u,\"person_id\":\"%s\"}",
             (unsigned)fp_slot, person_id);
    return post_event(body);
}

static esp_err_t cloud_post_slot_cleared(uint16_t fp_slot)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"type\":\"slot_cleared\",\"fp_slot\":%u}", (unsigned)fp_slot);
    return post_event(body);
}

static esp_err_t cloud_post_all_cleared(void)
{
    return post_event("{\"type\":\"all_cleared\"}");
}

static esp_err_t cloud_post_ack(int64_t command_seq)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"type\":\"ack\",\"command_seq\":%lld}", (long long)command_seq);
    esp_err_t err = post_event(body);
    if (err == ESP_OK) {
        s_last_command_seq = command_seq;
        nvs_save_cmd_seq();
    }
    return err;
}

static esp_err_t cloud_enqueue(const cloud_msg_t *msg)
{
    if (s_msg_q == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_msg_q, msg, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void cloud_drain_queue(void)
{
    cloud_msg_t msg;
    while (xQueueReceive(s_msg_q, &msg, 0) == pdTRUE) {
        esp_err_t err = ESP_FAIL;
        switch (msg.type) {
        case CLOUD_MSG_SCAN:
            err = cloud_post_scan(msg.fp_slot);
            if (err == ESP_OK) {
                ESP_LOGI(CLOUD_TAG, "attendance sent slot=%u", (unsigned)msg.fp_slot);
            }
            break;
        case CLOUD_MSG_ENROLL_DONE:
            err = cloud_post_enroll_done(msg.fp_slot, msg.person_id);
            break;
        case CLOUD_MSG_SLOT_CLEARED:
            err = cloud_post_slot_cleared(msg.fp_slot);
            break;
        case CLOUD_MSG_ALL_CLEARED:
            err = cloud_post_all_cleared();
            break;
        case CLOUD_MSG_ACK:
            err = cloud_post_ack(msg.command_seq);
            break;
        default:
            break;
        }
        if (err != ESP_OK) {
            ESP_LOGW(CLOUD_TAG, "event failed: %s", esp_err_to_name(err));
        }
    }
}

static void cloud_worker_task(void *arg)
{
    (void)arg;
    bool time_ok = false;
    bool logged_online = false;

    for (;;) {
        cloud_drain_queue();

        if (!s_configured || !app_wifi_is_connected() || s_cmd_cb == NULL) {
            logged_online = false;
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }

        if (!time_ok) {
            time_ok = app_wifi_wait_time_sync(20000);
            if (!time_ok) {
                vTaskDelay(pdMS_TO_TICKS(POLL_MS));
                continue;
            }
            ESP_LOGI(CLOUD_TAG, "time synced");
        }

        bool force_sync = s_sync_requested;
        if (force_sync) {
            s_sync_requested = false;
        }

        /* Enroll/search hold the FP mutex in bursts; sync must not IDENTIFY/rebuild mid-session. */
        if (s_busy) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }

        app_cloud_sync_t *sync = &s_sync;
        esp_err_t err = do_sync(sync);
        if (err == ESP_OK) {
            if (!logged_online) {
                ESP_LOGI(CLOUD_TAG, "online mode=%s seq=%lld ack=%lld maps=%u",
                         sync->desired_mode, (long long)sync->command_seq,
                         (long long)s_last_command_seq, (unsigned)sync->mapping_count);
                logged_online = true;
            }
            if (s_sync_cb != NULL) {
                s_sync_cb(sync, s_sync_ctx);
            }
            if (s_cmd_cb != NULL) {
                if (sync->command_seq > s_last_command_seq) {
                    ESP_LOGI(CLOUD_TAG, "command seq=%lld mode=%s slot=%u",
                             (long long)sync->command_seq, sync->desired_mode,
                             (unsigned)sync->desired_fp_slot);
                }
                s_cmd_cb(sync, s_cmd_ctx);
            }
        } else {
            logged_online = false;
            ESP_LOGW(CLOUD_TAG, "sync failed: %s", esp_err_to_name(err));
        }

        bool cmd_pending = err == ESP_OK && sync->command_seq > s_last_command_seq;
        int delay_ms = POLL_MS;
        if (cmd_pending || force_sync) {
            delay_ms = POLL_MS_FAST;
        } else if (app_realtime_is_connected()) {
            delay_ms = POLL_MS_REALTIME;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t app_cloud_init(void)
{
    s_api_key[0] = '\0';
    s_base_url[0] = '\0';
    s_device_id[0] = '\0';
    s_last_command_seq = 0;
    s_configured = false;
    s_background_scan = false;

    if (s_msg_q == NULL) {
        s_msg_q = xQueueCreate(8, sizeof(cloud_msg_t));
        if (s_msg_q == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = nvs_load();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(CLOUD_TAG, "nvs load: %s", esp_err_to_name(err));
    }

    if (s_api_key[0] != '\0') {
        trim_str(s_api_key);
    }

#ifdef CONFIG_CLOUD_DEVICE_API_KEY
    if (CONFIG_CLOUD_DEVICE_API_KEY[0] != '\0') {
        strncpy(s_api_key, CONFIG_CLOUD_DEVICE_API_KEY, sizeof(s_api_key) - 1);
        s_api_key[sizeof(s_api_key) - 1] = '\0';
        trim_str(s_api_key);
        log_key_source("menuconfig (overrides NVS)");
    } else if (s_api_key[0] != '\0') {
        log_key_source("NVS");
    }
#else
    if (s_api_key[0] != '\0') {
        log_key_source("NVS");
    }
#endif

    s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0' &&
                   effective_publishable_key()[0] != '\0';
    if (s_configured) {
        ESP_LOGI(CLOUD_TAG, "configured url=%s", effective_url());
    } else {
        ESP_LOGW(CLOUD_TAG, "not configured — need URL, publishable key, device key");
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

bool app_cloud_has_pending(const app_cloud_sync_t *sync)
{
    if (sync == NULL || !sync->valid) {
        return false;
    }
    return sync->command_seq > s_last_command_seq;
}

esp_err_t app_cloud_provision(const char *api_key)
{
    if (api_key == NULL || strlen(api_key) < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[128];
    strncpy(key, api_key, sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';
    trim_str(key);
    if (strlen(key) < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_save_str(NVS_KEY_API, key);
    if (err != ESP_OK) {
        return err;
    }
    strncpy(s_api_key, key, sizeof(s_api_key) - 1);
    s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0' &&
                   effective_publishable_key()[0] != '\0';
    log_key_source("NVS (provision)");
    return ESP_OK;
}

esp_err_t app_cloud_clear_api_key(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        (void)nvs_erase_key(h, NVS_KEY_API);
        err = nvs_commit(h);
        nvs_close(h);
    }

#ifdef CONFIG_CLOUD_DEVICE_API_KEY
    if (CONFIG_CLOUD_DEVICE_API_KEY[0] != '\0') {
        strncpy(s_api_key, CONFIG_CLOUD_DEVICE_API_KEY, sizeof(s_api_key) - 1);
        s_api_key[sizeof(s_api_key) - 1] = '\0';
        trim_str(s_api_key);
        log_key_source("menuconfig (NVS cleared)");
    } else
#endif
    {
        s_api_key[0] = '\0';
    }

    s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0' &&
                   effective_publishable_key()[0] != '\0';
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
    s_configured = s_api_key[0] != '\0' && effective_url()[0] != '\0' &&
                   effective_publishable_key()[0] != '\0';
    return ESP_OK;
}

esp_err_t app_cloud_set_device_id(const char *device_id)
{
    if (device_id == NULL || strlen(device_id) < 8 || strlen(device_id) >= sizeof(s_device_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_save_str(NVS_KEY_DEVICE_ID, device_id);
    if (err != ESP_OK) {
        return err;
    }
    strncpy(s_device_id, device_id, sizeof(s_device_id) - 1);
    s_device_id[sizeof(s_device_id) - 1] = '\0';
    return ESP_OK;
}

const char *app_cloud_device_id(void)
{
    return s_device_id;
}

esp_err_t app_cloud_report_scan(uint16_t fp_slot)
{
    cloud_msg_t msg = {.type = CLOUD_MSG_SCAN, .fp_slot = fp_slot};
    return cloud_enqueue(&msg);
}

esp_err_t app_cloud_report_enroll_done(uint16_t fp_slot, const char *person_id)
{
    if (person_id == NULL || person_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    cloud_msg_t msg = {.type = CLOUD_MSG_ENROLL_DONE, .fp_slot = fp_slot};
    strncpy(msg.person_id, person_id, sizeof(msg.person_id) - 1);
    return cloud_enqueue(&msg);
}

esp_err_t app_cloud_report_slot_cleared(uint16_t fp_slot)
{
    cloud_msg_t msg = {.type = CLOUD_MSG_SLOT_CLEARED, .fp_slot = fp_slot};
    return cloud_enqueue(&msg);
}

esp_err_t app_cloud_report_all_cleared(void)
{
    cloud_msg_t msg = {.type = CLOUD_MSG_ALL_CLEARED};
    return cloud_enqueue(&msg);
}

esp_err_t app_cloud_request_sync(void)
{
    s_sync_requested = true;
    return ESP_OK;
}

void app_cloud_set_sync_callback(app_cloud_sync_cb_t on_sync, void *ctx)
{
    s_sync_cb = on_sync;
    s_sync_ctx = ctx;
}

void app_cloud_set_settings_callback(app_cloud_settings_cb_t on_settings, void *ctx)
{
    s_settings_cb = on_settings;
    s_settings_ctx = ctx;
}

bool app_cloud_background_scan_enabled(void)
{
    return s_background_scan;
}

bool app_cloud_set_background_scan(bool enabled)
{
    if (enabled == s_background_scan) {
        return false;
    }
    s_background_scan = enabled;
    ESP_LOGI(CLOUD_TAG, "background scan %s", enabled ? "ON" : "OFF");
    if (s_settings_cb != NULL) {
        s_settings_cb(s_settings_ctx);
    }
    return true;
}

const char *app_cloud_slot_label(uint16_t fp_slot)
{
    if (fp_slot < APP_FP_MIN_USER_ID || fp_slot > APP_FP_MAX_USER_ID) {
        return "slot ?";
    }
    const app_cloud_mapping_t *m = &s_slot_map[fp_slot - APP_FP_MIN_USER_ID];
    if (m->external_id[0] != '\0') {
        return m->external_id;
    }
    if (m->display_name[0] != '\0') {
        return m->display_name;
    }
    static char buf[16];
    snprintf(buf, sizeof(buf), "slot %u", (unsigned)fp_slot);
    return buf;
}

esp_err_t app_cloud_ack(int64_t command_seq)
{
    cloud_msg_t msg = {.type = CLOUD_MSG_ACK, .command_seq = command_seq};
    return cloud_enqueue(&msg);
}

esp_err_t app_cloud_ack_now(int64_t command_seq)
{
    esp_err_t err = cloud_post_ack(command_seq);
    if (err != ESP_OK) {
        ESP_LOGW(CLOUD_TAG, "ack_now failed seq=%lld", (long long)command_seq);
    }
    return err;
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
    BaseType_t ok = xTaskCreate(cloud_worker_task, "cloud", CLOUD_TASK_STACK, NULL, 4, NULL);
    if (ok == pdPASS) {
        s_task_started = true;
        return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}
