#include "app_fingerprint.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define FP_TAG "app_fp"

#define FP_UART_NUM        UART_NUM_1
#define FP_PIN_TX          APP_FP_PIN_TX
#define FP_PIN_RX          APP_FP_PIN_RX
#define FP_BAUD            APP_FP_BAUD

#define FP_DATA_START      0xF5
#define FP_DATA_END        0xF5

#define FP_CMD_ENROLL1     0x01
#define FP_CMD_ENROLL2     0x02
#define FP_CMD_ENROLL3     0x03
#define FP_CMD_DELETE      0x04
#define FP_CMD_CLEAR       0x05
#define FP_CMD_USERNUMB    0x09
#define FP_CMD_IDENTIFY    0x0B
#define FP_CMD_SEARCH      0x0C
#define FP_CMD_GET_IMAGE   0x23

#define FP_ACK_SUCCESS     0x00
#define FP_ACK_FAIL        0x01
#define FP_ACK_FULL        0x04
#define FP_ACK_NOUSER      0x05
#define FP_ACK_USER_OCCUPIED 0x06
#define FP_ACK_USER_EXIST  0x07
#define FP_ACK_TIMEOUT     0x08

#define FP_FRAME_LEN       8
#define FP_PAYLOAD_LEN     5
#define FP_RX_BUF_SIZE     192
#define FP_SEARCH_TIMEOUT_MS 12000

static SemaphoreHandle_t s_lock;
static bool s_ready;
static int s_uart_baud;
static uint16_t s_last_user_id;
static uint8_t s_stored_count;
static uint8_t s_last_ack;
static uint8_t s_rx_buf[FP_RX_BUF_SIZE];
static uint8_t s_tx_buf[FP_FRAME_LEN];

static uint8_t fp_xor_checksum(uint8_t len, const uint8_t *data)
{
    uint8_t cs = 0;
    for (uint8_t i = 0; i < len; i++) {
        cs ^= data[i];
    }
    return cs;
}

static esp_err_t fp_send_packet(uint8_t len, const uint8_t *payload)
{
    if (len + 3 > sizeof(s_tx_buf)) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_tx_buf[0] = FP_DATA_START;
    memcpy(&s_tx_buf[1], payload, len);
    s_tx_buf[len + 1] = fp_xor_checksum(len, payload);
    s_tx_buf[len + 2] = FP_DATA_END;

    int written = uart_write_bytes(FP_UART_NUM, (const char *)s_tx_buf, len + 3);
    if (written != (int)(len + 3)) {
        return ESP_FAIL;
    }
    return uart_wait_tx_done(FP_UART_NUM, pdMS_TO_TICKS(500)) == ESP_OK ? ESP_OK : ESP_FAIL;
}

static bool fp_validate_frame(void)
{
    if (s_rx_buf[0] != FP_DATA_START || s_rx_buf[FP_FRAME_LEN - 1] != FP_DATA_END) {
        return false;
    }
    return s_rx_buf[6] == fp_xor_checksum(FP_PAYLOAD_LEN, &s_rx_buf[1]);
}

static bool fp_read_frame(uint8_t expect_cmd, int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 50) {
        uint8_t b = 0;
        int n = uart_read_bytes(FP_UART_NUM, &b, 1, pdMS_TO_TICKS(50));
        if (n != 1) {
            continue;
        }
        if (b != FP_DATA_START) {
            continue;
        }

        s_rx_buf[0] = b;
        int got = uart_read_bytes(FP_UART_NUM, &s_rx_buf[1], FP_FRAME_LEN - 1, pdMS_TO_TICKS(2000));
        if (got != FP_FRAME_LEN - 1) {
            continue;
        }
        if (!fp_validate_frame()) {
            ESP_LOGD(FP_TAG, "cmd 0x%02x: bad frame", expect_cmd);
            ESP_LOG_BUFFER_HEX_LEVEL(FP_TAG, s_rx_buf, FP_FRAME_LEN, ESP_LOG_DEBUG);
            continue;
        }
        if (s_rx_buf[1] != expect_cmd) {
            ESP_LOGD(FP_TAG, "cmd 0x%02x: skip stale 0x%02x", expect_cmd, s_rx_buf[1]);
            continue;
        }
        return true;
    }
    return false;
}

static uint8_t fp_check_package(uint8_t cmd)
{

    switch (cmd) {
    case FP_CMD_ENROLL1:
    case FP_CMD_ENROLL2:
    case FP_CMD_ENROLL3:
        if (s_rx_buf[4] == FP_ACK_SUCCESS) {
            return FP_ACK_SUCCESS;
        }
        return s_rx_buf[4];
    case FP_CMD_GET_IMAGE:
    case FP_CMD_DELETE:
    case FP_CMD_CLEAR:
        if (s_rx_buf[4] == FP_ACK_SUCCESS) {
            return FP_ACK_SUCCESS;
        }
        return s_rx_buf[4];
    case FP_CMD_IDENTIFY:
        if (s_rx_buf[4] == FP_ACK_SUCCESS) {
            s_last_user_id = ((uint16_t)s_rx_buf[2] << 8) | s_rx_buf[3];
            if (s_last_user_id == 0) {
                s_last_user_id = s_rx_buf[3];
            }
            return FP_ACK_SUCCESS;
        }
        return s_rx_buf[4];
    case FP_CMD_USERNUMB: {
        uint16_t n = ((uint16_t)s_rx_buf[2] << 8) | s_rx_buf[3];
        if (s_rx_buf[4] == FP_ACK_SUCCESS) {
            if (n == 0 && s_rx_buf[3] != 0) {
                n = s_rx_buf[3];
            }
            s_stored_count = (n > 255) ? 255 : (uint8_t)n;
            s_last_user_id = s_stored_count;
            return FP_ACK_SUCCESS;
        }
        if (n > 0 && n <= APP_FP_MAX_USER_ID) {
            s_stored_count = (uint8_t)n;
            s_last_user_id = s_stored_count;
            return FP_ACK_SUCCESS;
        }
        return s_rx_buf[4];
    }
    case FP_CMD_SEARCH: {
        /*
         * SEARCH success: byte4 = match quality 1–3 (NOT ACK_FAIL — that is also 0x01).
         * Ref: f5 0c 00 01 01 00 0c f5 → user 1, level 1.
         */
        uint8_t level = s_rx_buf[4];
        if (level >= 1 && level <= 3) {
            uint16_t uid = s_rx_buf[3];
            if (uid < APP_FP_MIN_USER_ID && s_rx_buf[2] >= APP_FP_MIN_USER_ID) {
                uid = s_rx_buf[2];
            }
            if (uid >= APP_FP_MIN_USER_ID) {
                s_last_user_id = uid;
                return FP_ACK_SUCCESS;
            }
        }
        if (level == FP_ACK_SUCCESS) {
            uint16_t uid = ((uint16_t)s_rx_buf[2] << 8) | s_rx_buf[3];
            if (uid == 0 && s_rx_buf[3] >= APP_FP_MIN_USER_ID) {
                uid = s_rx_buf[3];
            }
            if (uid >= APP_FP_MIN_USER_ID) {
                s_last_user_id = uid;
                return FP_ACK_SUCCESS;
            }
        }
        if (level == FP_ACK_NOUSER || level == FP_ACK_TIMEOUT) {
            return FP_ACK_NOUSER;
        }
        return FP_ACK_NOUSER;
    }
    default:
        return FP_ACK_FAIL;
    }
}

static void fp_log_rx_snippet(const char *label)
{
    size_t avail = 0;
    if (uart_get_buffered_data_len(FP_UART_NUM, &avail) != ESP_OK || avail == 0) {
        return;
    }
    uint8_t buf[32];
    int n = uart_read_bytes(FP_UART_NUM, buf, sizeof(buf), 0);
    if (n > 0) {
        ESP_LOGW(FP_TAG, "%s: %d RX bytes (check baud/wiring):", label, n);
        ESP_LOG_BUFFER_HEX_LEVEL(FP_TAG, buf, n, ESP_LOG_WARN);
    }
}

static void fp_log_last_frame(const char *label)
{
    ESP_LOGW(FP_TAG, "%s RX:", label);
    ESP_LOG_BUFFER_HEX_LEVEL(FP_TAG, s_rx_buf, FP_FRAME_LEN, ESP_LOG_WARN);
}

static esp_err_t fp_run_cmd_timeout(uint8_t cmd, uint16_t arg, uint8_t arg_byte3, int timeout_ms,
                                    bool flush_rx)
{
    uint8_t payload[5] = {
        cmd,
        (uint8_t)(arg >> 8),
        (uint8_t)(arg & 0xFF),
        arg_byte3,
        0x00,
    };

    if (flush_rx) {
        uart_flush_input(FP_UART_NUM);
    }

    esp_err_t err = fp_send_packet(FP_PAYLOAD_LEN, payload);
    if (err != ESP_OK) {
        return err;
    }
    if (!fp_read_frame(cmd, timeout_ms)) {
        ESP_LOGD(FP_TAG, "cmd 0x%02x: no valid response", cmd);
        fp_log_rx_snippet("timeout");
        return ESP_ERR_TIMEOUT;
    }

    uint8_t ack = fp_check_package(cmd);
    if (ack == FP_ACK_SUCCESS) {
        s_last_ack = 0;
        return ESP_OK;
    }
    s_last_ack = ack;
    if (cmd == FP_CMD_SEARCH && ack == FP_ACK_NOUSER) {
        ESP_LOGD(FP_TAG, "SEARCH no match frame");
        ESP_LOG_BUFFER_HEX_LEVEL(FP_TAG, s_rx_buf, FP_FRAME_LEN, ESP_LOG_DEBUG);
        return ESP_ERR_NOT_FOUND;
    }

    fp_log_last_frame("cmd");
    ESP_LOGD(FP_TAG, "cmd 0x%02x ack=0x%02x", cmd, ack);
    return ESP_FAIL;
}

static esp_err_t fp_run_cmd(uint8_t cmd, uint16_t arg, uint8_t arg_byte3)
{
    int timeout_ms = 3000;
    if (cmd == FP_CMD_SEARCH) {
        timeout_ms = FP_SEARCH_TIMEOUT_MS;
    } else if (cmd == FP_CMD_ENROLL1 || cmd == FP_CMD_ENROLL2 || cmd == FP_CMD_ENROLL3 ||
               cmd == FP_CMD_GET_IMAGE || cmd == FP_CMD_IDENTIFY || cmd == FP_CMD_SEARCH) {
        timeout_ms = 20000;
    }
    return fp_run_cmd_timeout(cmd, arg, arg_byte3, timeout_ms, true);
}

/** Caller must hold s_lock. */
static void fp_refresh_count_locked(void)
{
    if (fp_run_cmd(FP_CMD_USERNUMB, 0, 0) == ESP_OK) {
        (void)fp_check_package(FP_CMD_USERNUMB);
    }
}

static esp_err_t fp_set_baud(int baud)
{
    const uart_config_t uart_cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(FP_UART_NUM, &uart_cfg);
    uart_flush_input(FP_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(50));
    return err;
}

#define FP_PING_TIMEOUT_MS  1500

static esp_err_t fp_ping_module(int tx_gpio, int rx_gpio)
{
    esp_err_t err = uart_set_pin(FP_UART_NUM, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(350));
    uart_flush_input(FP_UART_NUM);

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    err = fp_run_cmd_timeout(FP_CMD_USERNUMB, 0, 0, FP_PING_TIMEOUT_MS, true);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        fp_log_rx_snippet("after USERNUMB");
    }
    return err;
}

static esp_err_t fp_detect_module(int *tx_out, int *rx_out)
{
    int tx0 = APP_FP_SWAP_TX_RX ? APP_FP_PIN_RX : APP_FP_PIN_TX;
    int rx0 = APP_FP_SWAP_TX_RX ? APP_FP_PIN_TX : APP_FP_PIN_RX;

    ESP_LOGI(FP_TAG, "probing module at %d baud...", APP_FP_BAUD);

    esp_err_t err = fp_ping_module(tx0, rx0);
    if (err == ESP_OK) {
        *tx_out = tx0;
        *rx_out = rx0;
        return ESP_OK;
    }

    err = fp_ping_module(rx0, tx0);
    if (err == ESP_OK) {
        ESP_LOGW(FP_TAG, "module OK with TX/RX swapped — swap dupont wires");
        *tx_out = rx0;
        *rx_out = tx0;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t app_fp_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = uart_driver_install(FP_UART_NUM, FP_RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(FP_TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return err;
    }

    if (fp_set_baud(APP_FP_BAUD) != ESP_OK) {
        uart_driver_delete(FP_UART_NUM);
        return ESP_FAIL;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        uart_driver_delete(FP_UART_NUM);
        return ESP_ERR_NO_MEM;
    }

    vTaskDelay(pdMS_TO_TICKS(800));

    int tx = 0;
    int rx = 0;
    s_uart_baud = APP_FP_BAUD;
    err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (attempt > 1) {
            ESP_LOGW(FP_TAG, "probe retry %d/3...", attempt);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        err = fp_detect_module(&tx, &rx);
        if (err == ESP_OK) {
            break;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(FP_TAG, "FPC1020A not responding at %d baud", APP_FP_BAUD);
        ESP_LOGE(FP_TAG, "  Try: ESP GPIO4 <-> module RX, GPIO7 <-> module TX (swap if needed)");
        ESP_LOGE(FP_TAG, "  3V3 + GND, module LED on when powered");
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        uart_driver_delete(FP_UART_NUM);
        return err;
    }

    uart_flush_input(FP_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(100));

    s_ready = true;
    uint8_t n = 0;
    if (app_fp_get_user_count(&n) == ESP_OK) {
        ESP_LOGI(FP_TAG, "stored templates: %u", (unsigned)n);
    }
    ESP_LOGI(FP_TAG, "FPC1020A OK — %d baud TX=GPIO%d RX=GPIO%d", s_uart_baud, tx, rx);
    return ESP_OK;
}

bool app_fp_is_ready(void)
{
    return s_ready;
}

uint16_t app_fp_last_user_id(void)
{
    return s_last_user_id;
}

uint8_t app_fp_last_ack(void)
{
    return s_last_ack;
}

const char *app_fp_ack_str(uint8_t ack)
{
    switch (ack) {
    case FP_ACK_SUCCESS:
        return "OK";
    case FP_ACK_FAIL:
        return "failed (lift/replace finger)";
    case FP_ACK_FULL:
        return "database full";
    case FP_ACK_NOUSER:
        return "no user";
    case FP_ACK_USER_OCCUPIED:
        return "slot already used";
    case FP_ACK_USER_EXIST:
        return "fingerprint already enrolled";
    case FP_ACK_TIMEOUT:
        return "timeout (place finger sooner)";
    default:
        return "unknown error";
    }
}

esp_err_t app_fp_test_sensor(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(25000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = fp_run_cmd_timeout(FP_CMD_GET_IMAGE, 0, 0, 20000, true);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t app_fp_enroll_step(uint16_t slot_id, int step)
{
    if (!s_ready || slot_id < APP_FP_MIN_USER_ID || slot_id > APP_FP_MAX_USER_ID || step < 1 ||
        step > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd = (step == 1) ? FP_CMD_ENROLL1 : (step == 2) ? FP_CMD_ENROLL2 : FP_CMD_ENROLL3;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(120000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ESP_ERR_TIMEOUT;
    const int max_tries = 25;

    for (int attempt = 1; attempt <= max_tries; attempt++) {
        bool flush = (attempt > 1);
        err = fp_run_cmd_timeout(cmd, slot_id, 1, 20000, flush);
        if (err == ESP_OK) {
            break;
        }
        if (s_last_ack != FP_ACK_FAIL && s_last_ack != FP_ACK_TIMEOUT && err != ESP_ERR_TIMEOUT) {
            break;
        }
        ESP_LOGI(FP_TAG, "enroll step %d try %d/%d: %s",
                 step, attempt, max_tries,
                 err == ESP_ERR_TIMEOUT ? "no response" : app_fp_ack_str(s_last_ack));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (err == ESP_OK && step == 3) {
        fp_refresh_count_locked();
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK && step < 3) {
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    return err;
}

esp_err_t app_fp_enroll(uint16_t slot_id)
{
    for (int step = 1; step <= 3; step++) {
        esp_err_t err = app_fp_enroll_step(slot_id, step);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t app_fp_identify(uint16_t slot_id)
{
    if (!s_ready || slot_id < APP_FP_MIN_USER_ID || slot_id > APP_FP_MAX_USER_ID) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(25000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = fp_run_cmd_timeout(FP_CMD_IDENTIFY, slot_id, 0, 20000, true);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t app_fp_search(uint16_t *out_id, bool *matched)
{
    if (!s_ready || out_id == NULL || matched == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *matched = false;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(90000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* User needs time to move finger from button to sensor after "scan". */
    vTaskDelay(pdMS_TO_TICKS(1200));

    esp_err_t err = ESP_FAIL;
    for (int try_n = 1; try_n <= 12; try_n++) {
        /* Only flush UART before the first attempt — retries may have a late reply. */
        err = fp_run_cmd_timeout(FP_CMD_SEARCH, 0, 0, 20000, try_n == 1);
        if (err == ESP_OK) {
            *out_id = s_last_user_id;
            *matched = true;
            xSemaphoreGive(s_lock);
            return ESP_OK;
        }
        if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_TIMEOUT) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT) {
        fp_log_last_frame("SEARCH final");
        *matched = false;
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }

    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t app_fp_delete(uint16_t slot_id)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = fp_run_cmd(FP_CMD_DELETE, slot_id, 0);
    if (err == ESP_OK) {
        fp_refresh_count_locked();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t app_fp_clear_all(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = fp_run_cmd_timeout(FP_CMD_CLEAR, 0, 0, 15000, true);
    if (err == ESP_OK || s_last_ack == FP_ACK_NOUSER) {
        /* Module returns NOUSER when the store is already empty. */
        s_stored_count = 0;
        s_last_ack = 0;
        err = ESP_OK;
    } else {
        ESP_LOGW(FP_TAG, "clear failed ack=0x%02x", s_last_ack);
    }
    xSemaphoreGive(s_lock);
    return err;
}

uint8_t app_fp_stored_count(void)
{
    return s_stored_count;
}

esp_err_t app_fp_get_user_count(uint8_t *count)
{
    if (!s_ready || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = fp_run_cmd(FP_CMD_USERNUMB, 0, 0);
    if (err == ESP_OK) {
        *count = s_stored_count;
    }
    xSemaphoreGive(s_lock);
    return err;
}
