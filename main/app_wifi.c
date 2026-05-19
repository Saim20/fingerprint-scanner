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
#include "sdkconfig.h"

#include "app_buzzer.h"
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
static bool s_had_connection;
static uint32_t s_ip_addr;

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

static void wifi_show_oled(const char *line2, const char *line3)
{
    if (app_oled_is_ready()) {
        app_oled_show_lines("WiFi", line2, line3, "");
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_had_connection) {
            app_buzzer_beep_wifi(false);
        }
        s_connected = false;
        s_ip_addr = 0;
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "retry connect (%d/%d)", s_retry_num, CONFIG_ESP_MAXIMUM_RETRY);
            wifi_show_oled("Connecting...", CONFIG_ESP_WIFI_SSID);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(WIFI_TAG, "failed after %d retries", CONFIG_ESP_MAXIMUM_RETRY);
            wifi_show_oled("Failed", CONFIG_ESP_WIFI_SSID);
            app_buzzer_beep_error();
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
