#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#include "app_oled.h"

#define WIFI_TAG "app_wifi"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

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
static int s_retry_num;
static volatile bool s_connected;
static uint32_t s_ip_addr;

static void wifi_show_oled(const char *line2, const char *line3)
{
    if (app_oled_is_ready()) {
        app_oled_show_lines("WiFi", line2, line3, "");
    }
}

static const char *wifi_reason_str(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
        return "auth expired";
    case WIFI_REASON_AUTH_LEAVE:
        return "auth leave";
    case WIFI_REASON_ASSOC_EXPIRE:
        return "assoc expired";
    case WIFI_REASON_ASSOC_TOOMANY:
        return "AP full";
    case WIFI_REASON_NOT_AUTHED:
        return "not authenticated";
    case WIFI_REASON_NOT_ASSOCED:
        return "not associated";
    case WIFI_REASON_ASSOC_LEAVE:
        return "assoc leave";
    case WIFI_REASON_ASSOC_NOT_AUTHED:
        return "assoc not authed";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4-way timeout (wrong password?)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake timeout";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection fail";
    case WIFI_REASON_AUTH_FAIL:
        return "auth fail (wrong password or WPA mode?)";
    case WIFI_REASON_NO_AP_FOUND:
        return "SSID not found";
    default:
        return "see esp_wifi_types.h";
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        s_connected = false;
        s_ip_addr = 0;
        ESP_LOGW(WIFI_TAG, "disconnected: reason=%u (%s)", (unsigned)disc->reason,
                 wifi_reason_str(disc->reason));
        if (disc->reason == WIFI_REASON_AUTH_FAIL ||
            disc->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            disc->reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
            ESP_LOGW(WIFI_TAG, "check menuconfig SSID/password and set auth to WPA2/WPA3 if AP uses WPA3");
        }
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "retry connect (%d/%d)", s_retry_num, CONFIG_ESP_MAXIMUM_RETRY);
            wifi_show_oled("Connecting...", CONFIG_ESP_WIFI_SSID);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(WIFI_TAG, "failed after %d retries", CONFIG_ESP_MAXIMUM_RETRY);
            wifi_show_oled("Failed", CONFIG_ESP_WIFI_SSID);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_retry_num = 0;
        s_connected = true;
        s_ip_addr = event->ip_info.ip.addr;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(WIFI_TAG, "connected, ip: " IPSTR, IP2STR(&event->ip_info.ip));
        char ip_line[20];
        snprintf(ip_line, sizeof(ip_line), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_show_oled("Connected", ip_line);
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

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

    ESP_LOGI(WIFI_TAG, "connecting to SSID:%s", CONFIG_ESP_WIFI_SSID);
    wifi_show_oled("Connecting...", CONFIG_ESP_WIFI_SSID);
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
