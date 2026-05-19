/*
 * Phase 1: fingerprint attendance — enroll, verify, scan (serial + buttons).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_buzzer.h"
#include "app_buttons.h"
#include "app_fingerprint.h"
#include "app_oled.h"
#include "app_wifi.h"

static const char *TAG = "attendance";

typedef enum {
    ACT_ENROLL_NEXT,
    ACT_SCAN,
    ACT_TOGGLE_AUTO,
    ACT_DELETE_LAST,
    ACT_CLEAR_ALL,
} action_t;

static bool s_fp_ok;
static bool s_auto_scan;
static bool s_busy;
static volatile bool s_action_pending;
static uint16_t s_next_enroll_id;
static uint16_t s_last_enrolled_id;
static bool s_has_enrolled;
static volatile bool s_startup_done;
static QueueHandle_t s_action_q;

/** Visible in idf.py monitor (OLED may be off). */
static void msg_user(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void msg_enroll_err(int step, esp_err_t err)
{
    if (err == ESP_ERR_TIMEOUT) {
        msg_user("  -> step %d: TIMEOUT — place finger on sensor, wait for LED\n", step);
    } else if (app_fp_last_ack() != 0) {
        msg_user("  -> step %d: %s (ack 0x%02x)\n", step, app_fp_ack_str(app_fp_last_ack()),
                 app_fp_last_ack());
    } else {
        msg_user("  -> step %d: %s\n", step, esp_err_to_name(err));
    }
}

static void show_idle_screen(void)
{
    uint8_t count = 0;
    char count_line[24];
    char hint_line[24];
    if (s_fp_ok && app_fp_get_user_count(&count) == ESP_OK) {
        snprintf(count_line, sizeof(count_line), "Users: %u", (unsigned)count);
    } else {
        snprintf(count_line, sizeof(count_line), "FP: not ready");
    }
    snprintf(hint_line, sizeof(hint_line), "Btn0 enroll ID%u", (unsigned)s_next_enroll_id);
    app_oled_show_lines("Attendance", "Place finger", count_line,
                        s_auto_scan ? hint_line : "Auto scan off");
}

static void sync_template_count(void)
{
    uint8_t count = 0;
    if (s_fp_ok && app_fp_get_user_count(&count) == ESP_OK) {
        s_has_enrolled = count > 0;
        if (count == 0) {
            s_next_enroll_id = APP_FP_MIN_USER_ID;
            s_last_enrolled_id = 0;
        } else if (count < APP_FP_MAX_SLOTS) {
            s_next_enroll_id = (uint16_t)(APP_FP_MIN_USER_ID + count);
        } else {
            s_next_enroll_id = APP_FP_MAX_USER_ID;
        }
        /* After reboot s_last_enrolled_id is 0; guess slot for delete button. */
        if (count > 0 && s_last_enrolled_id < APP_FP_MIN_USER_ID) {
            s_last_enrolled_id = (s_next_enroll_id > APP_FP_MIN_USER_ID)
                                     ? (uint16_t)(s_next_enroll_id - 1)
                                     : APP_FP_MIN_USER_ID;
        }
    }
}

static uint16_t guess_delete_slot(void)
{
    if (s_last_enrolled_id >= APP_FP_MIN_USER_ID && s_last_enrolled_id <= APP_FP_MAX_USER_ID) {
        return s_last_enrolled_id;
    }
    if (s_next_enroll_id > APP_FP_MIN_USER_ID) {
        return (uint16_t)(s_next_enroll_id - 1);
    }
    return APP_FP_MIN_USER_ID;
}

static bool run_enroll(uint16_t slot_id)
{
    if (!s_fp_ok) {
        msg_user("\n[ENROLL] Fingerprint module not ready.\n");
        return false;
    }

    msg_user("\n========================================\n");
    msg_user("  ENROLL fingerprint -> template slot %u\n", (unsigned)slot_id);
    msg_user("  Scan the SAME finger 3 times.\n");
    msg_user("  Lift finger between each scan.\n");
    msg_user("========================================\n\n");

    esp_err_t err = ESP_OK;
    for (int step = 1; step <= 3 && err == ESP_OK; step++) {
        msg_user("[ENROLL] Step %d/3 — press finger flat on sensor, HOLD 3-5 sec\n", step);
        msg_user("           (module scans when LED is on; retries ~25s)\n");
        app_oled_show_lines("ENROLL", "Hold finger", "", "");
        err = app_fp_enroll_step(slot_id, step);
        if (err == ESP_OK) {
            msg_user("[ENROLL] Step %d/3 OK\n", step);
            app_buzzer_beep_ok();
        } else {
            msg_enroll_err(step, err);
            app_buzzer_beep_deny();
            break;
        }
    }

    if (err != ESP_OK) {
        msg_user("\n*** ENROLL FAILED (slot %u) ***\n", (unsigned)slot_id);
        if (app_fp_last_ack() == 0x06) {
            msg_user("  Slot already used — type: delete %u  then enroll %u\n\n",
                     (unsigned)slot_id, (unsigned)slot_id);
        } else if (app_fp_last_ack() == 0x07) {
            msg_user("  Finger already enrolled — use scan or try another slot\n\n");
        } else {
            msg_user("\n");
        }
        app_oled_show_lines("ENROLL", "Failed", "", "");
        return false;
    }

    sync_template_count();
    uint8_t stored = 0;
    app_fp_get_user_count(&stored);

    msg_user("\n*** ENROLL SUCCESS ***\n");
    msg_user("  Template saved in slot %u\n", (unsigned)slot_id);
    msg_user("  Total templates in module: %u\n\n", (unsigned)stored);

    s_last_enrolled_id = slot_id;
    s_has_enrolled = true;
    if (s_next_enroll_id == slot_id && s_next_enroll_id < APP_FP_MAX_USER_ID) {
        s_next_enroll_id++;
    }
    app_oled_show_lines("ENROLL", "Success", "", "");
    app_buzzer_beep_ok();
    return true;
}

static bool run_verify_scan(uint16_t expect_id)
{
    msg_user("----------------------------------------\n");
    msg_user("  VERIFY: place the SAME finger, hold 3-5 sec\n");
    msg_user("----------------------------------------\n");

    vTaskDelay(pdMS_TO_TICKS(1200));

    if (app_fp_identify(expect_id) == ESP_OK) {
        msg_user("[VERIFY] MATCH OK — slot %u (1:1 identify)\n", (unsigned)expect_id);
        msg_user("  Fingerprint system is working.\n\n");
        app_buzzer_beep_ok();
        return true;
    }

    uint16_t user_id = 0;
    bool matched = false;
    esp_err_t err = app_fp_search(&user_id, &matched);
    if (err == ESP_OK && matched) {
        msg_user("[VERIFY] MATCH OK — ID %u (1:N search)\n", (unsigned)user_id);
        app_buzzer_beep_ok();
        return true;
    }

    msg_user("[VERIFY] NO MATCH — try: scan (hold finger) or enroll again\n");
    if (app_fp_last_ack() != 0) {
        msg_user("  last ack: %s (0x%02x)\n", app_fp_ack_str(app_fp_last_ack()), app_fp_last_ack());
    }
    return false;
}

static void handle_match(uint16_t user_id)
{
    s_last_enrolled_id = user_id;
    s_has_enrolled = true;
    msg_user("[SCAN] MATCH — welcome, ID %u\n", (unsigned)user_id);
    app_oled_show_lines("MATCH", "OK", "", "");
    app_buzzer_beep_ok();
    vTaskDelay(pdMS_TO_TICKS(1500));
    show_idle_screen();
}

static void handle_no_match(void)
{
    ESP_LOGD(TAG, "no match");
    vTaskDelay(pdMS_TO_TICKS(300));
}

static void run_scan_once(void)
{
    if (!s_fp_ok) {
        msg_user("[SCAN] module not ready\n");
        return;
    }

    uint8_t count = app_fp_stored_count();
    if (count == 0) {
        app_fp_get_user_count(&count);
    }
    if (count == 0) {
        msg_user("[SCAN] No templates — enroll 1 first (type: enroll 1)\n");
        return;
    }

    msg_user("[SCAN] %u template(s) — put finger on sensor NOW, hold 3-5 sec\n", (unsigned)count);
    app_oled_show_lines("SCAN", "Place finger", "", "");
    vTaskDelay(pdMS_TO_TICKS(500));

    uint16_t user_id = 0;
    bool matched = false;
    esp_err_t err = app_fp_search(&user_id, &matched);
    if (err == ESP_OK && matched) {
        handle_match(user_id);
    } else if (err == ESP_OK) {
        msg_user("[SCAN] No match");
        if (app_fp_last_ack() != 0) {
            msg_user(" (module: %s)\n", app_fp_ack_str(app_fp_last_ack()));
        } else {
            msg_user(" — lift finger, press scan again\n");
        }
        handle_no_match();
    } else {
        msg_user("[SCAN] Error: %s\n", esp_err_to_name(err));
    }
}

static void run_delete_last(void)
{
    if (!s_fp_ok) {
        msg_user("[DELETE] module not ready\n");
        return;
    }

    uint8_t count = 0;
    app_fp_get_user_count(&count);
    if (count == 0) {
        msg_user("[DELETE] Nothing to delete\n");
        s_has_enrolled = false;
        return;
    }

    uint16_t primary = guess_delete_slot();

    if (app_fp_delete(primary) == ESP_OK) {
        msg_user("[DELETE] Removed slot %u\n", (unsigned)primary);
        sync_template_count();
        app_buzzer_beep_ok();
        return;
    }

    for (int id = APP_FP_MAX_USER_ID; id >= APP_FP_MIN_USER_ID; id--) {
        if ((uint16_t)id == primary) {
            continue;
        }
        if (app_fp_delete((uint16_t)id) == ESP_OK) {
            msg_user("[DELETE] Removed slot %d\n", id);
            sync_template_count();
            app_buzzer_beep_ok();
            return;
        }
    }

    msg_user("[DELETE] Failed slot %u", (unsigned)primary);
    if (app_fp_last_ack() != 0) {
        msg_user(" (%s)\n", app_fp_ack_str(app_fp_last_ack()));
    } else {
        msg_user("\n");
    }
    app_buzzer_beep_deny();
}

static void run_clear_all(void)
{
    if (!s_fp_ok) {
        return;
    }
    uint8_t count = 0;
    app_fp_get_user_count(&count);
    if (count == 0) {
        msg_user("[CLEAR] Already empty\n");
        s_next_enroll_id = APP_FP_MIN_USER_ID;
        s_has_enrolled = false;
        s_auto_scan = false;
        return;
    }
    if (app_fp_clear_all() == ESP_OK) {
        msg_user("[CLEAR] All templates erased\n");
        s_next_enroll_id = APP_FP_MIN_USER_ID;
        s_has_enrolled = false;
        s_auto_scan = false;
        sync_template_count();
        app_buzzer_beep_ok();
        return;
    }
    msg_user("[CLEAR] bulk erase failed (ack 0x%02x) — deleting slots 1..%d\n",
             app_fp_last_ack(), APP_FP_MAX_USER_ID);
    int removed = 0;
    for (int id = APP_FP_MIN_USER_ID; id <= APP_FP_MAX_USER_ID; id++) {
        if (app_fp_delete((uint16_t)id) == ESP_OK) {
            removed++;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    sync_template_count();
    uint8_t left = 0;
    app_fp_get_user_count(&left);
    if (left == 0) {
        msg_user("[CLEAR] Done (%d slot(s) removed)\n", removed);
        s_next_enroll_id = APP_FP_MIN_USER_ID;
        s_has_enrolled = false;
        s_auto_scan = false;
        app_buzzer_beep_ok();
    } else {
        msg_user("[CLEAR] %u template(s) remain — try: delete 1, delete 2, ...\n", (unsigned)left);
        app_buzzer_beep_deny();
    }
}

static void post_action(action_t act)
{
    if (s_action_q == NULL || s_action_pending) {
        return;
    }
    if (xQueueSend(s_action_q, &act, 0) != pdTRUE) {
        ESP_LOGW(TAG, "action queue busy");
        return;
    }
    s_action_pending = true;
}

static void on_button(app_btn_id_t btn, bool long_press, void *ctx)
{
    (void)ctx;
    if (s_busy) {
        ESP_LOGW(TAG, "busy — ignore button");
        return;
    }
    if (!s_fp_ok && btn != APP_BTN_AUTO) {
        msg_user("[BTN] Fingerprint module not ready\n");
        return;
    }

    switch (btn) {
    case APP_BTN_ENROLL:
        post_action(ACT_ENROLL_NEXT);
        break;
    case APP_BTN_SCAN:
        post_action(ACT_SCAN);
        break;
    case APP_BTN_AUTO:
        post_action(ACT_TOGGLE_AUTO);
        break;
    case APP_BTN_DELETE:
        post_action(long_press ? ACT_CLEAR_ALL : ACT_DELETE_LAST);
        break;
    default:
        break;
    }
}

static void ui_worker_task(void *arg)
{
    (void)arg;
    action_t act;

    for (;;) {
        if (xQueueReceive(s_action_q, &act, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_action_pending = false;

        bool was_auto = s_auto_scan;
        s_auto_scan = false;
        s_busy = true;

        switch (act) {
        case ACT_ENROLL_NEXT:
            if (run_enroll(s_next_enroll_id)) {
                run_verify_scan(s_last_enrolled_id);
            }
            break;
        case ACT_SCAN:
            run_scan_once();
            break;
        case ACT_TOGGLE_AUTO:
            s_auto_scan = !was_auto;
            msg_user("[AUTO] Background scan %s\n", s_auto_scan ? "ON" : "OFF");
            show_idle_screen();
            s_busy = false;
            continue;
        case ACT_DELETE_LAST:
            run_delete_last();
            break;
        case ACT_CLEAR_ALL:
            run_clear_all();
            break;
        default:
            break;
        }

        s_busy = false;
        s_auto_scan = was_auto;
    }
}

static void startup_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(300));

    if (!s_fp_ok) {
        msg_user("\n*** Fingerprint module NOT detected ***\n");
        msg_user("Check: 3V3, GND, dupont TX/RX (try swapping)\n");
        msg_user("      GPIO4 + GPIO7 to module (not GPIO0-3)\n\n");
        vTaskDelete(NULL);
        return;
    }

    uint8_t count = 0;
    app_fp_get_user_count(&count);
    sync_template_count();

    if (count == 0) {
        msg_user("\n############################################\n");
        msg_user("#  NO FINGERPRINTS — enroll user ID 1      #\n");
        msg_user("############################################\n");
        msg_user("1) Put finger on the sensor (cover the glass)\n");
        msg_user("2) Type: touch   (checks sensor sees finger)\n");
        msg_user("3) Type: enroll 1   or press enroll (GPIO0)\n\n");
    } else {
        msg_user("\n[READY] %u template(s). GPIO0=enroll GPIO1=scan GPIO2=auto GPIO3=delete\n",
                 (unsigned)count);
        msg_user("  auto scan OFF — type 'auto on' or press GPIO2\n\n");
    }

    char ip_str[16];
    if (app_wifi_ip_str(ip_str, sizeof(ip_str))) {
        msg_user("[WiFi] connected (%s)\n", ip_str);
    } else {
        msg_user("[WiFi] not connected yet (check menuconfig SSID/password)\n");
    }

    if (app_oled_is_ready()) {
        msg_user("[OLED] display OK\n");
    } else {
        msg_user("[OLED] not detected — type: oled  (scan I2C pins)\n");
    }

    show_idle_screen();
    s_startup_done = true;
    vTaskDelete(NULL);
}

static void print_help(void)
{
    msg_user("\nCommands:\n");
    msg_user("  enroll <id>    - store fingerprint (3 scans, same finger)\n");
    msg_user("  scan           - test 1:N match\n");
    msg_user("  auto on|off    - background scanning\n");
    msg_user("  delete <id>    - remove one template\n");
    msg_user("  clear          - erase all templates\n");
    msg_user("  touch          - test if sensor sees your finger\n");
    msg_user("  buttons        - show button GPIO levels\n");
    msg_user("  count          - templates stored in module\n");
    msg_user("  oled           - scan I2C for OLED (debug wiring)\n");
    msg_user("  help           - this message\n");
    msg_user("\nButtons (C3 Super Mini): GPIO0=enroll GPIO1=scan GPIO2=auto GPIO3=delete\n");
    msg_user("         type 'buttons' to test wiring\n\n");
}

static void handle_serial_line(char *line)
{
    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    if (*line == '\0') {
        return;
    }

    char *cmd = strtok(line, " \t\r\n");
    if (cmd == NULL) {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        print_help();
    } else if (strcmp(cmd, "buttons") == 0) {
        app_buttons_print_levels();
    } else if (strcmp(cmd, "count") == 0) {
        uint8_t n = app_fp_stored_count();
        if (s_fp_ok) {
            app_fp_get_user_count(&n);
        }
        msg_user("Templates stored: %u (use IDs %d-%d)\n", (unsigned)n, APP_FP_MIN_USER_ID,
                 APP_FP_MAX_USER_ID);
    } else if (strcmp(cmd, "enroll") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (arg == NULL) {
            msg_user("usage: enroll <id>\n");
            return;
        }
        int id = atoi(arg);
        if (id < APP_FP_MIN_USER_ID || id > APP_FP_MAX_USER_ID) {
            msg_user("id must be %d-%d\n", APP_FP_MIN_USER_ID, APP_FP_MAX_USER_ID);
            return;
        }
        s_busy = true;
        if (run_enroll((uint16_t)id)) {
            run_verify_scan((uint16_t)id);
        }
        s_busy = false;
    } else if (strcmp(cmd, "touch") == 0) {
        if (!s_fp_ok) {
            msg_user("fingerprint module not ready\n");
            return;
        }
        msg_user("[TOUCH] Place finger on sensor now...\n");
        s_busy = true;
        if (app_fp_test_sensor() == ESP_OK) {
            msg_user("[TOUCH] OK — sensor detected your finger\n");
            app_buzzer_beep_ok();
        } else if (app_fp_last_ack() != 0) {
            msg_user("[TOUCH] No — %s (ack 0x%02x)\n", app_fp_ack_str(app_fp_last_ack()),
                     app_fp_last_ack());
            app_buzzer_beep_deny();
        } else {
            msg_user("[TOUCH] No response — check finger placement / wiring\n");
        }
        s_busy = false;
    } else if (strcmp(cmd, "scan") == 0) {
        s_busy = true;
        run_scan_once();
        s_busy = false;
    } else if (strcmp(cmd, "auto") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (arg && strcmp(arg, "scan") == 0) {
            arg = strtok(NULL, " \t\r\n");
        }
        if (arg && strcmp(arg, "on") == 0) {
            s_auto_scan = true;
            msg_user("auto scan on\n");
        } else if (arg && strcmp(arg, "off") == 0) {
            s_auto_scan = false;
            msg_user("auto scan off\n");
        } else {
            msg_user("usage: auto on|off\n");
        }
    } else if (strcmp(cmd, "delete") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (arg == NULL) {
            msg_user("usage: delete <id>\n");
            return;
        }
        int id = atoi(arg);
        if (id < APP_FP_MIN_USER_ID || id > APP_FP_MAX_USER_ID) {
            msg_user("id must be %d-%d\n", APP_FP_MIN_USER_ID, APP_FP_MAX_USER_ID);
            return;
        }
        if (s_fp_ok && app_fp_delete((uint16_t)id) == ESP_OK) {
            msg_user("deleted slot %d\n", id);
            sync_template_count();
        } else {
            msg_user("delete %d failed", id);
            if (app_fp_last_ack() == 0x05) {
                msg_user(" (no template in that slot)\n");
            } else if (app_fp_last_ack() != 0) {
                msg_user(" (%s)\n", app_fp_ack_str(app_fp_last_ack()));
            } else {
                msg_user("\n");
            }
        }
    } else if (strcmp(cmd, "clear") == 0) {
        s_busy = true;
        run_clear_all();
        s_busy = false;
    } else if (strcmp(cmd, "oled") == 0) {
        app_oled_diag();
    } else {
        msg_user("unknown: %s (try help)\n", cmd);
    }
}

/** Read one full line from USB monitor (fgets returns per-char on ESP USB-JTAG). */
static bool serial_read_line(char *line, size_t line_size)
{
    size_t n = 0;
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (n == 0) {
                continue;
            }
            break;
        }
        if (c == '\b' || c == 127) {
            if (n > 0) {
                n--;
                msg_user("\b \b");
            }
            continue;
        }
        if (n + 1 < line_size) {
            line[n++] = (char)c;
        }
    }
    line[n] = '\0';
    return true;
}

static void serial_cmd_task(void *arg)
{
    (void)arg;
    while (!s_startup_done) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    print_help();
    char line[80];
    for (;;) {
        msg_user("> ");
        serial_read_line(line, sizeof(line));
        handle_serial_line(line);
    }
}

static void attendance_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));

    while (true) {
        if (s_auto_scan && s_fp_ok && !s_busy) {
            uint8_t count = 0;
            if (app_fp_get_user_count(&count) != ESP_OK || count == 0) {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            uint16_t user_id = 0;
            bool matched = false;
            esp_err_t err = app_fp_search(&user_id, &matched);
            if (err == ESP_OK && matched) {
                s_busy = true;
                handle_match(user_id);
                s_busy = false;
            } else {
                vTaskDelay(pdMS_TO_TICKS(400));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(400));
        }
    }
}

void app_main(void)
{
    printf("\n\n=== Fingerprint attendance boot ===\n");
    fflush(stdout);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_auto_scan = false;

    s_action_q = xQueueCreate(1, sizeof(action_t));
    if (s_action_q == NULL) {
        ESP_LOGE(TAG, "action queue create failed");
        return;
    }

    /* OLED before WiFi — avoids I2C probe spam overlapping WiFi connect logs. */
    esp_err_t oled_err = app_oled_init();
    if (oled_err != ESP_OK) {
        msg_user("[BOOT] External OLED not detected (%s)\n", esp_err_to_name(oled_err));
        msg_user("  SDA=GPIO5 SCL=GPIO6 VCC=3V3 GND=GND (not a built-in display)\n");
    }

    if (app_wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed");
    }

    app_buzzer_init();

    msg_user("\n[BOOT] Initializing fingerprint module...\n");
    s_fp_ok = app_fp_init() == ESP_OK;
    if (s_fp_ok) {
        uint8_t fp_count = 0;
        if (app_fp_get_user_count(&fp_count) == ESP_OK) {
            ESP_LOGI(TAG, "templates stored: %u", (unsigned)fp_count);
        }
        msg_user("[BOOT] Fingerprint module OK\n");
    } else {
        msg_user("[BOOT] Fingerprint module FAILED — check UART wiring\n");
    }

    app_buttons_init(on_button, NULL);

    xTaskCreate(ui_worker_task, "ui_worker", 4096, NULL, 5, NULL);
    xTaskCreate(startup_task, "startup", 4096, NULL, 5, NULL);
    xTaskCreate(serial_cmd_task, "serial_cmd", 4096, NULL, 4, NULL);
    xTaskCreate(attendance_task, "attendance", 4096, NULL, 3, NULL);
}
