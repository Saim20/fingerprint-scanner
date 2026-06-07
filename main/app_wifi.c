#include "app_wifi.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "app_buzzer.h"

#define WIFI_TAG "app_wifi"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/** After max fast retries, wait this long before trying again (never give up). */
#define WIFI_BACKOFF_MS    30000

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define APP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define APP_WIFI_H2E_ID   ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define APP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define APP_WIFI_H2E_ID   CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define APP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define APP_WIFI_H2E_ID   CONFIG_ESP_WIFI_PW_ID
#endif

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define APP_WIFI_AUTH_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

static EventGroupHandle_t s_wifi_event_group;
static TimerHandle_t s_backoff_timer;
static int s_retry_num;
static volatile bool s_connected;
static bool s_had_connection;
static uint32_t s_ip_addr;
static uint8_t s_last_disconnect_reason;

static const char *wifi_reason_str(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_UNSPECIFIED:
        return "unspecified";
    case WIFI_REASON_AUTH_EXPIRE:
        return "auth expired";
    case WIFI_REASON_AUTH_LEAVE:
        return "auth leave";
    case WIFI_REASON_ASSOC_TOOMANY:
        return "AP full";
    case WIFI_REASON_ASSOC_LEAVE:
        return "disassociated";
    case WIFI_REASON_ASSOC_NOT_AUTHED:
        return "assoc not authed";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD:
        return "bad power cap";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
        return "bad channel";
    case WIFI_REASON_IE_INVALID:
        return "invalid IE";
    case WIFI_REASON_MIC_FAILURE:
        return "MIC failure";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4-way handshake timeout (wrong password?)";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
        return "group key timeout";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS:
        return "IE mismatch";
    case WIFI_REASON_GROUP_CIPHER_INVALID:
        return "group cipher invalid";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
        return "pairwise cipher invalid";
    case WIFI_REASON_AKMP_INVALID:
        return "AKMP invalid";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
        return "RSN IE version";
    case WIFI_REASON_INVALID_RSN_IE_CAP:
        return "RSN IE cap invalid";
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return "802.1X auth failed";
    case WIFI_REASON_CIPHER_SUITE_REJECTED:
        return "cipher rejected";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon timeout (AP out of range?)";
    case WIFI_REASON_NO_AP_FOUND:
        return "SSID not found (2.4 GHz only)";
    case WIFI_REASON_AUTH_FAIL:
        return "auth failed (wrong password?)";
    case WIFI_REASON_ASSOC_FAIL:
        return "assoc failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake timeout";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection failed";
    case WIFI_REASON_AP_TSF_RESET:
        return "AP TSF reset";
    case WIFI_REASON_ROAMING:
        return "roaming";
    case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
        return "assoc comeback timeout";
    case WIFI_REASON_SA_QUERY_TIMEOUT:
        return "SA query timeout";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "no AP with compatible security";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "auth mode below threshold (try WPA2/WPA3 in menuconfig)";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "signal too weak";
    default:
        return "unknown";
    }
}

static void wifi_backoff_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    ESP_LOGI(WIFI_TAG, "backoff done — reconnecting to %s", CONFIG_ESP_WIFI_SSID);
    esp_wifi_connect();
}

static void wifi_start_sntp(void)
{
    if (esp_sntp_enabled()) {
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(WIFI_TAG, "SNTP started");
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        s_last_disconnect_reason = disc->reason;
        ESP_LOGW(WIFI_TAG, "disconnected: %s (%d)", wifi_reason_str(disc->reason), disc->reason);
        if (s_had_connection) {
            app_buzzer_beep_wifi(false);
        }
        s_connected = false;
        s_ip_addr = 0;
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "retry connect (%d/%d)", s_retry_num, CONFIG_ESP_MAXIMUM_RETRY);
        } else if (s_backoff_timer != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(WIFI_TAG, "fast retries exhausted — next try in %ds", WIFI_BACKOFF_MS / 1000);
            app_buzzer_beep_error();
            xTimerStart(s_backoff_timer, 0);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_retry_num = 0;
        s_connected = true;
        s_had_connection = true;
        s_ip_addr = event->ip_info.ip.addr;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(WIFI_TAG, "connected, ip: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_start_sntp();
        app_buzzer_beep_wifi(true);
    }
}

esp_err_t app_wifi_init(void)
{
    if (s_wifi_event_group != NULL) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* Drop stale SSID/BSSID cached by ESP-IDF before esp_wifi_init reads NVS. */
    nvs_handle_t nvs_wifi;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &nvs_wifi) == ESP_OK) {
        if (nvs_erase_all(nvs_wifi) == ESP_OK) {
            (void)nvs_commit(nvs_wifi);
            ESP_LOGI(WIFI_TAG, "cleared stale Wi-Fi NVS cache");
        }
        nvs_close(nvs_wifi);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* Menuconfig is authoritative — ignore stale credentials cached in NVS. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = CONFIG_ESP_WIFI_SSID,
                .password = CONFIG_ESP_WIFI_PASSWORD,
                .threshold.authmode = APP_WIFI_AUTH_THRESHOLD,
#if defined(APP_WIFI_SAE_MODE)
                .sae_pwe_h2e = APP_WIFI_SAE_MODE,
                .sae_h2e_identifier = APP_WIFI_H2E_ID,
#endif
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
                .disable_wpa3_compatible_mode = 0,
#endif
            },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_backoff_timer = xTimerCreate("wifi_backoff", pdMS_TO_TICKS(WIFI_BACKOFF_MS), pdFALSE, NULL,
                                   wifi_backoff_timer_cb);
    if (s_backoff_timer == NULL) {
        ESP_LOGW(WIFI_TAG, "backoff timer create failed — no auto-retry after fast retries");
    }

    ESP_LOGI(WIFI_TAG, "connecting to SSID:%s", CONFIG_ESP_WIFI_SSID);
    return ESP_OK;
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

uint32_t app_wifi_get_ip(void)
{
    return s_ip_addr;
}

bool app_wifi_ip_str(char *buf, size_t buf_len)
{
    if (!s_connected || buf == NULL || buf_len == 0) {
        if (buf != NULL && buf_len > 0) {
            buf[0] = '\0';
        }
        return false;
    }
    esp_ip4_addr_t ip = {.addr = s_ip_addr};
    snprintf(buf, buf_len, IPSTR, IP2STR(&ip));
    return true;
}

esp_err_t app_wifi_clear_stored_config(void)
{
    nvs_handle_t h;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_erase_all(h);
        (void)nvs_commit(h);
        nvs_close(h);
    }
    esp_err_t err = esp_wifi_restore();
    if (err != ESP_OK) {
        return err;
    }
    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = CONFIG_ESP_WIFI_SSID,
                .password = CONFIG_ESP_WIFI_PASSWORD,
                .threshold.authmode = APP_WIFI_AUTH_THRESHOLD,
#if defined(APP_WIFI_SAE_MODE)
                .sae_pwe_h2e = APP_WIFI_SAE_MODE,
                .sae_h2e_identifier = APP_WIFI_H2E_ID,
#endif
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
                .disable_wpa3_compatible_mode = 0,
#endif
            },
    };
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

esp_err_t app_wifi_reconnect(void)
{
    if (s_backoff_timer != NULL) {
        xTimerStop(s_backoff_timer, 0);
    }
    s_retry_num = 0;
    s_last_disconnect_reason = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

void app_wifi_print_status(void)
{
    printf("[WiFi] SSID (menuconfig): %s\n", CONFIG_ESP_WIFI_SSID);
    printf("[WiFi] connected: %s\n", s_connected ? "yes" : "no");
    char ip[16];
    if (app_wifi_ip_str(ip, sizeof(ip))) {
        printf("[WiFi] IP: %s\n", ip);
    }
    if (!s_connected && s_last_disconnect_reason != 0) {
        printf("[WiFi] last error: %s (%u)\n", wifi_reason_str(s_last_disconnect_reason),
               (unsigned)s_last_disconnect_reason);
    }
    if (s_retry_num > 0) {
        printf("[WiFi] fast retries: %d/%d\n", s_retry_num, CONFIG_ESP_MAXIMUM_RETRY);
    }
}

bool app_wifi_wait_time_sync(uint32_t timeout_ms)
{
    const time_t min_valid = 1700000000; /* ~2023 — cert validation needs real time */
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS < timeout_ms) {
        time_t now = 0;
        time(&now);
        if (now >= min_valid) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}
