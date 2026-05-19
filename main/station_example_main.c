/*
 * Cloud-first fingerprint attendance — web dashboard controls the device;
 * GPIO0 runs pending cloud commands; always-on scan when idle.
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
#include "app_fp_bare.h"
#include "app_fingerprint.h"
#include "app_oled.h"
#include "app_realtime.h"
#include "app_wifi.h"
#include "app_cloud.h"

static const char *TAG = "attendance";

typedef enum {
    ACT_RUN_PENDING,
    ACT_BARE_ENROLL,
} action_t;

static bool s_fp_ok;
static bool s_busy;
static volatile bool s_action_pending;
static uint16_t s_next_enroll_id;
static volatile bool s_startup_done;
static QueueHandle_t s_action_q;
static app_cloud_sync_t s_pending_cmd;
static int64_t s_last_auto_cmd_seq;
static int64_t s_last_auto_attempt_tick;
static int64_t s_last_notify_cmd_seq;

static void show_status_screen(void);
static void on_cloud_command(const app_cloud_sync_t *cmd, void *ctx);

static bool cloud_command_needs_go(const app_cloud_sync_t *cmd)
{
    if (cmd == NULL || !cmd->valid) {
        return false;
    }
    return strcmp(cmd->desired_mode, "add") == 0 || strcmp(cmd->desired_mode, "scan") == 0;
}

static bool attendance_paused(void)
{
    if (s_busy || s_action_pending) {
        return true;
    }
    if (!app_cloud_background_scan_enabled()) {
        return true;
    }
    /* Pause for any pending cloud command — delete/clear need exclusive UART access. */
    return app_cloud_has_pending(&s_pending_cmd);
}

static bool cloud_command_auto_runs(const app_cloud_sync_t *cmd)
{
    if (cmd == NULL || !cmd->valid) {
        return false;
    }
    return strcmp(cmd->desired_mode, "delete") == 0 || strcmp(cmd->desired_mode, "clear") == 0;
}

static void try_post_pending(void)
{
    if (s_busy || s_action_pending || s_action_q == NULL) {
        return;
    }
    action_t act = ACT_RUN_PENDING;
    if (xQueueSend(s_action_q, &act, 0) != pdTRUE) {
        return;
    }
    s_action_pending = true;
}

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
        msg_user("  -> step %d: TIMEOUT — press finger on sensor when you hear prompt beep\n", step);
    } else if (app_fp_last_ack() != 0) {
        msg_user("  -> step %d: %s (ack 0x%02x)\n", step, app_fp_ack_str(app_fp_last_ack()),
                 app_fp_last_ack());
    } else {
        msg_user("  -> step %d: %s\n", step, esp_err_to_name(err));
    }
}

static const char *cloud_status_line(void)
{
    if (!app_cloud_is_configured()) {
        return "Cloud: setup";
    }
    if (!app_wifi_is_connected()) {
        return "WiFi: waiting";
    }
    return "Cloud OK";
}

static void show_pending_screen(void)
{
    const char *mode = s_pending_cmd.desired_mode;
    const char *who = s_pending_cmd.person_display_name[0] ? s_pending_cmd.person_display_name
                                                           : s_pending_cmd.desired_person_id;
    char line3[24];

    if (strcmp(mode, "add") == 0) {
        snprintf(line3, sizeof(line3), "next slot %u", (unsigned)s_next_enroll_id);
        app_oled_show_lines("Press GO", "Enroll", who[0] ? who : "person", line3);
    } else if (strcmp(mode, "scan") == 0) {
        app_oled_show_lines("Press GO", "Test scan", "", "");
    } else if (strcmp(mode, "delete") == 0) {
        snprintf(line3, sizeof(line3), "slot %u", (unsigned)s_pending_cmd.desired_fp_slot);
        app_oled_show_lines("Cloud", "Deleting", line3, "");
    } else if (strcmp(mode, "clear") == 0) {
        app_oled_show_lines("Cloud", "Clear all", "", "");
    } else {
        show_status_screen();
    }
}

static void show_status_screen(void)
{
    uint8_t count = 0;
    char count_line[24];

    if (s_fp_ok && app_fp_get_user_count(&count) == ESP_OK) {
        snprintf(count_line, sizeof(count_line), "Users: %u", (unsigned)count);
    } else {
        snprintf(count_line, sizeof(count_line), "FP: not ready");
    }

    if (app_cloud_has_pending(&s_pending_cmd)) {
        show_pending_screen();
        return;
    }

    if (app_cloud_background_scan_enabled()) {
        app_oled_show_lines("Attendance", "Scan finger", count_line, cloud_status_line());
    } else {
        app_oled_show_lines("Command mode", "Use dashboard", count_line, cloud_status_line());
    }
}

static void sync_template_count(void)
{
    uint8_t count = 0;
    if (s_fp_ok && app_fp_get_user_count(&count) == ESP_OK) {
        if (count == 0) {
            s_next_enroll_id = APP_FP_MIN_USER_ID;
        } else if (count < APP_FP_MAX_SLOTS) {
            s_next_enroll_id = (uint16_t)(APP_FP_MIN_USER_ID + count);
        } else {
            s_next_enroll_id = APP_FP_MAX_USER_ID;
        }
    }
}

static bool run_enroll(uint16_t slot_id)
{
    if (!s_fp_ok) {
        msg_user("\n[ENROLL] Fingerprint module not ready.\n");
        app_buzzer_beep_warn();
        return false;
    }

    app_buzzer_beep_start();

    msg_user("\n========================================\n");
    msg_user("  ENROLL fingerprint -> template slot %u\n", (unsigned)slot_id);
    msg_user("  Scan the SAME finger %d times.\n", APP_FP_ENROLL_STEPS);
    msg_user("  Lift finger between each step.\n");
    msg_user("========================================\n\n");

    esp_err_t err = ESP_OK;
    for (int step = 1; step <= APP_FP_ENROLL_STEPS && err == ESP_OK; step++) {
        msg_user("[ENROLL] Step %d/%d — press flat, HOLD 3-5 sec\n", step, APP_FP_ENROLL_STEPS);
        msg_user("           Lift between steps; retries ~25s\n");
        app_oled_show_lines("ENROLL", "Hold finger", "", "");
        err = app_fp_enroll_step(slot_id, step);
        if (err == ESP_OK) {
            msg_user("[ENROLL] Step %d/%d OK\n", step, APP_FP_ENROLL_STEPS);
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
            msg_user("  Slot already used — delete slot %u first\n\n", (unsigned)slot_id);
        } else if (app_fp_last_ack() == 0x07) {
            msg_user("  Finger already enrolled in another slot — delete it first via dashboard\n\n");
        } else {
            msg_user("\n");
        }
        (void)app_fp_clear_slot(slot_id);
        app_oled_show_lines("ENROLL", "Failed", "", "");
        return false;
    }

    /* 0788220 treated all captures OK as success — IDENTIFY often stays NOUSER on this module. */
    (void)app_fp_mark_slot_enrolled(slot_id);
    sync_template_count();
    uint8_t stored = 0;
    app_fp_get_user_count(&stored);

    msg_user("\n*** ENROLL SUCCESS ***\n");
    msg_user("  Template saved in slot %u\n", (unsigned)slot_id);
    msg_user("  Total templates in module: %u\n\n", (unsigned)stored);

    if (s_next_enroll_id == slot_id && s_next_enroll_id < APP_FP_MAX_USER_ID) {
        s_next_enroll_id++;
    }
    app_oled_show_lines("ENROLL", "Success", "", "");
    app_buzzer_beep_done();
    app_cloud_request_sync();
    return true;
}

static void handle_match(uint16_t user_id)
{
    const char *label = app_cloud_is_configured() ? app_cloud_slot_label(user_id) : NULL;
    if (label != NULL && label[0] != '\0') {
        msg_user("[SCAN] MATCH — %s (slot %u)\n", label, (unsigned)user_id);
    } else {
        msg_user("[SCAN] MATCH — welcome, ID %u\n", (unsigned)user_id);
    }
    if (app_cloud_is_configured()) {
        if (app_cloud_report_scan(user_id) == ESP_OK) {
            msg_user("[CLOUD] attendance queued\n");
        } else {
            msg_user("[CLOUD] queue failed\n");
            app_buzzer_beep_warn();
        }
    }
    char line2[24];
    snprintf(line2, sizeof(line2), "slot %u", (unsigned)user_id);
    app_oled_show_lines("MATCH", label ? label : "OK", line2, "");
    app_buzzer_beep_ok();
    vTaskDelay(pdMS_TO_TICKS(1500));
    show_status_screen();
}

static void handle_no_match(void)
{
    msg_user("[SCAN] No match\n");
    app_buzzer_beep_no_match();
    app_oled_show_lines("NO MATCH", "Unknown", "finger", "");
    vTaskDelay(pdMS_TO_TICKS(1200));
    if (!s_busy) {
        show_status_screen();
    }
}

static void run_scan_once(void)
{
    if (!s_fp_ok) {
        msg_user("[SCAN] module not ready\n");
        app_buzzer_beep_warn();
        return;
    }

    uint8_t count = app_fp_stored_count();
    if (count == 0) {
        app_fp_get_user_count(&count);
    }
    if (count == 0) {
        msg_user("[SCAN] No templates on device\n");
        app_buzzer_beep_warn();
        return;
    }

    app_buzzer_beep_start();
    msg_user("[SCAN] %u template(s) — put finger on sensor NOW, hold 3-5 sec\n", (unsigned)count);
    app_oled_show_lines("SCAN", "Place finger", "", "");
    app_buzzer_beep_prompt();
    vTaskDelay(pdMS_TO_TICKS(500));

    uint16_t user_id = 0;
    bool matched = false;
    esp_err_t err = app_fp_search(&user_id, &matched);
    if (err == ESP_OK && matched) {
        handle_match(user_id);
    } else if (err == ESP_OK) {
        handle_no_match();
    } else {
        msg_user("[SCAN] Error: %s\n", esp_err_to_name(err));
        app_buzzer_beep_deny();
    }
}

static bool run_delete_slot(uint16_t slot_id)
{
    if (!s_fp_ok) {
        msg_user("[DELETE] module not ready\n");
        app_buzzer_beep_warn();
        return false;
    }
    app_buzzer_beep_start();
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (app_fp_delete(slot_id) == ESP_OK) {
            msg_user("[DELETE] Removed slot %u\n", (unsigned)slot_id);
            if (app_cloud_is_configured()) {
                app_cloud_report_slot_cleared(slot_id);
            }
            sync_template_count();
            app_cloud_request_sync();
            app_buzzer_beep_ok();
            return true;
        }
        if (app_fp_last_ack() == 0x05) {
            msg_user("[DELETE] Slot %u already empty\n", (unsigned)slot_id);
            if (app_cloud_is_configured()) {
                app_cloud_report_slot_cleared(slot_id);
            }
            sync_template_count();
            app_cloud_request_sync();
            app_buzzer_beep_cancel();
            return true;
        }
        if (attempt < 3) {
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
    msg_user("[DELETE] Failed slot %u", (unsigned)slot_id);
    if (app_fp_last_ack() != 0) {
        msg_user(" (%s)\n", app_fp_ack_str(app_fp_last_ack()));
    } else {
        msg_user("\n");
    }
    app_buzzer_beep_deny();
    return false;
}

static bool run_clear_all(void)
{
    if (!s_fp_ok) {
        msg_user("[CLEAR] module not ready\n");
        app_buzzer_beep_warn();
        return false;
    }
    app_buzzer_beep_start();
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (app_fp_clear_all() == ESP_OK) {
            msg_user("[CLEAR] All templates removed\n");
            if (app_cloud_is_configured()) {
                app_cloud_report_all_cleared();
            }
            sync_template_count();
            app_cloud_request_sync();
            app_buzzer_beep_done();
            return true;
        }
        if (attempt < 3) {
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
    msg_user("[CLEAR] Failed");
    if (app_fp_last_ack() != 0) {
        msg_user(" (%s)\n", app_fp_ack_str(app_fp_last_ack()));
    } else {
        msg_user("\n");
    }
    app_buzzer_beep_deny();
    return false;
}

static void post_action(action_t act)
{
    if (s_action_q == NULL || s_action_pending) {
        return;
    }
    if (xQueueSend(s_action_q, &act, 0) != pdTRUE) {
        ESP_LOGW(TAG, "action queue busy");
        app_buzzer_beep_busy();
        return;
    }
    s_action_pending = true;
}

static void run_pending_cloud_command(void)
{
    const char *mode = s_pending_cmd.desired_mode;

    if (strcmp(mode, "add") == 0) {
        const char *who = s_pending_cmd.person_display_name[0]
                              ? s_pending_cmd.person_display_name
                              : s_pending_cmd.desired_person_id;
        uint16_t slot = 0;
        if (app_fp_alloc_enroll_slot(&slot) != ESP_OK) {
            msg_user("[CLOUD] Enroll failed — module full (no free slots)\n");
            app_buzzer_beep_deny();
            return;
        }
        msg_user("[CLOUD] Enroll %s → module slot %u (auto)\n", who, (unsigned)slot);
        /* Clear partial/failed enroll so ENROLL2 does not stay stuck. */
        (void)app_fp_clear_slot(slot);
        vTaskDelay(pdMS_TO_TICKS(150));
        if (run_enroll(slot)) {
            if (s_pending_cmd.desired_person_id[0] != '\0') {
                app_cloud_report_enroll_done(slot, s_pending_cmd.desired_person_id);
            }
            app_cloud_ack(s_pending_cmd.command_seq);
        }
    } else if (strcmp(mode, "scan") == 0) {
        msg_user("[CLOUD] Remote scan\n");
        run_scan_once();
        app_cloud_ack(s_pending_cmd.command_seq);
    } else if (strcmp(mode, "delete") == 0) {
        uint16_t slot = s_pending_cmd.desired_fp_slot;
        if (slot >= APP_FP_MIN_USER_ID && slot <= APP_FP_MAX_USER_ID) {
            msg_user("[CLOUD] Remote delete slot %u\n", (unsigned)slot);
            if (run_delete_slot(slot)) {
                app_cloud_ack(s_pending_cmd.command_seq);
            }
        } else {
            msg_user("[CLOUD] Invalid delete slot %u — clearing command\n", (unsigned)slot);
            app_buzzer_beep_warn();
            app_cloud_ack(s_pending_cmd.command_seq);
        }
    } else if (strcmp(mode, "clear") == 0) {
        msg_user("[CLOUD] Remote clear all\n");
        if (run_clear_all()) {
            app_cloud_ack(s_pending_cmd.command_seq);
        }
    }
}

static void on_cloud_settings(void *ctx)
{
    (void)ctx;
    if (s_startup_done) {
        app_buzzer_beep_mode(app_cloud_background_scan_enabled());
    }
    if (!s_busy) {
        show_status_screen();
    }
}

static void on_cloud_sync(const app_cloud_sync_t *sync, void *ctx)
{
    (void)ctx;
    if (sync == NULL) {
        return;
    }

    static struct {
        uint8_t unmapped_count;
        uint8_t stale_count;
        uint16_t unmapped[16];
        uint16_t stale[16];
    } last;

    if (sync->unmapped_count > 0 || sync->stale_count > 0) {
        bool same = sync->unmapped_count == last.unmapped_count &&
                    sync->stale_count == last.stale_count;
        if (same) {
            for (uint8_t i = 0; i < sync->unmapped_count && same; i++) {
                if (sync->unmapped_slots[i] != last.unmapped[i]) {
                    same = false;
                }
            }
            for (uint8_t i = 0; i < sync->stale_count && same; i++) {
                if (sync->stale_slots[i] != last.stale[i]) {
                    same = false;
                }
            }
        }
        if (!same) {
            last.unmapped_count = sync->unmapped_count;
            last.stale_count = sync->stale_count;
            memcpy(last.unmapped, sync->unmapped_slots, sizeof(last.unmapped));
            memcpy(last.stale, sync->stale_slots, sizeof(last.stale));
            ESP_LOGI(TAG, "cloud: template drift — %u unmapped, %u stale (map in web UI)",
                     (unsigned)sync->unmapped_count, (unsigned)sync->stale_count);
            if (s_startup_done) {
                app_buzzer_beep_warn();
            }
        }
    }

    if (app_cloud_has_pending(&s_pending_cmd) &&
        sync->command_seq == s_pending_cmd.command_seq) {
        if (sync->person_display_name[0] != '\0') {
            strncpy(s_pending_cmd.person_display_name, sync->person_display_name,
                    sizeof(s_pending_cmd.person_display_name) - 1);
        }
        if (sync->person_external_id[0] != '\0') {
            strncpy(s_pending_cmd.person_external_id, sync->person_external_id,
                    sizeof(s_pending_cmd.person_external_id) - 1);
        }
        if (s_pending_cmd.desired_fp_slot == 0 && sync->desired_fp_slot > 0) {
            s_pending_cmd.desired_fp_slot = sync->desired_fp_slot;
        }
    }

    if (!s_busy) {
        show_status_screen();
    }
}

static void on_cloud_command(const app_cloud_sync_t *cmd, void *ctx)
{
    (void)ctx;
    if (cmd == NULL || !cmd->valid) {
        return;
    }

    if (strcmp(cmd->desired_mode, "idle") != 0 &&
        cmd->command_seq <= app_cloud_last_command_seq()) {
        return;
    }

    memcpy(&s_pending_cmd, cmd, sizeof(s_pending_cmd));

    const app_cloud_sync_t *last = app_cloud_last_sync();
    if (last != NULL && last->valid && last->command_seq == cmd->command_seq) {
        if (last->person_display_name[0] != '\0') {
            strncpy(s_pending_cmd.person_display_name, last->person_display_name,
                    sizeof(s_pending_cmd.person_display_name) - 1);
        }
        if (last->person_external_id[0] != '\0') {
            strncpy(s_pending_cmd.person_external_id, last->person_external_id,
                    sizeof(s_pending_cmd.person_external_id) - 1);
        }
        if (s_pending_cmd.desired_fp_slot == 0 && last->desired_fp_slot > 0) {
            s_pending_cmd.desired_fp_slot = last->desired_fp_slot;
        }
    }

    if (strcmp(cmd->desired_mode, "idle") == 0) {
        if (app_cloud_has_pending(cmd)) {
            app_cloud_ack_now(cmd->command_seq);
            msg_user("[CLOUD] Command cancelled (seq %lld)\n", (long long)cmd->command_seq);
            app_buzzer_beep_cancel();
        }
        s_last_notify_cmd_seq = 0;
        memset(&s_pending_cmd, 0, sizeof(s_pending_cmd));
        if (!s_busy) {
            show_status_screen();
        }
        return;
    }

    if (cloud_command_auto_runs(cmd) && app_cloud_has_pending(cmd)) {
        int64_t tick = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (cmd->command_seq == s_last_auto_cmd_seq &&
            (tick - s_last_auto_attempt_tick) < 2000) {
            return;
        }
        s_last_auto_cmd_seq = cmd->command_seq;
        s_last_auto_attempt_tick = tick;
        if (cmd->command_seq != s_last_notify_cmd_seq) {
            s_last_notify_cmd_seq = cmd->command_seq;
            app_buzzer_beep_notify();
        }
        if (strcmp(cmd->desired_mode, "clear") == 0) {
            ESP_LOGI(TAG, "cloud: auto-clear seq=%lld", (long long)cmd->command_seq);
        } else {
            ESP_LOGI(TAG, "cloud: auto-delete slot %u seq=%lld",
                     (unsigned)cmd->desired_fp_slot, (long long)cmd->command_seq);
        }
        try_post_pending();
        return;
    }

    if (app_cloud_has_pending(cmd)) {
        if (cloud_command_needs_go(cmd)) {
            ESP_LOGI(TAG, "cloud: pending %s seq=%lld — press GO",
                     cmd->desired_mode, (long long)cmd->command_seq);
            if (cmd->command_seq != s_last_notify_cmd_seq) {
                s_last_notify_cmd_seq = cmd->command_seq;
                app_buzzer_beep_notify();
            }
        }
        if (!s_busy) {
            show_pending_screen();
        }
    } else if (!s_busy) {
        show_status_screen();
    }
}

static void on_go_button(void *ctx)
{
    (void)ctx;
    if (s_busy) {
        ESP_LOGW(TAG, "busy — ignore GO");
        app_buzzer_beep_busy();
        return;
    }
    if (!app_cloud_has_pending(&s_pending_cmd)) {
        msg_user("[BTN] No pending command — use web dashboard\n");
        app_buzzer_beep_deny();
        app_oled_show_lines("No command", "Use dashboard", "", "");
        vTaskDelay(pdMS_TO_TICKS(1200));
        show_status_screen();
        return;
    }
    if (!cloud_command_needs_go(&s_pending_cmd)) {
        msg_user("[BTN] Delete/clear/cancel run from cloud — no GO needed\n");
        app_buzzer_beep_warn();
        return;
    }
    app_buzzer_beep_go();
    post_action(ACT_RUN_PENDING);
}

static void on_bare_enroll_button(void *ctx)
{
    (void)ctx;
    if (!s_fp_ok) {
        msg_user("[BTN] Fingerprint module not ready\n");
        app_buzzer_beep_error();
        return;
    }
    if (s_busy) {
        ESP_LOGW(TAG, "busy — ignore ENROLL");
        app_buzzer_beep_busy();
        return;
    }
    app_buzzer_beep_go();
    post_action(ACT_BARE_ENROLL);
}

static void ui_worker_task(void *arg)
{
    (void)arg;
    action_t act;

    for (;;) {
        if (xQueueReceive(s_action_q, &act, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        s_busy = true;
        app_cloud_set_busy(true);
        if (act == ACT_RUN_PENDING) {
            run_pending_cloud_command();
        } else if (act == ACT_BARE_ENROLL) {
            app_fp_bare_enroll(APP_FP_BARE_SLOT);
        }
        s_busy = false;
        app_cloud_set_busy(false);
        s_action_pending = false;
        show_status_screen();
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
        app_buzzer_beep_error();
        s_startup_done = true;
        vTaskDelete(NULL);
        return;
    }

    uint8_t count = 0;
    app_fp_get_user_count(&count);
    sync_template_count();

    size_t reg_count = 0;
    app_fp_slots_list(NULL, 0, &reg_count);
    if (reg_count != count) {
        msg_user("[BOOT] Syncing slot registry with module (%u vs %u)…\n", (unsigned)reg_count,
                 (unsigned)count);
        app_fp_slots_rebuild_registry();
        sync_template_count();
    }

    msg_user("\n[READY] Cloud-controlled scanner\n");
    msg_user("  GPIO0 = run pending command from dashboard\n");
    msg_user("  GPIO1 = BARE enroll test (slot %u, 3 scans, no cloud)\n",
             (unsigned)APP_FP_BARE_SLOT);
    msg_user("  Passive scan: enable in web dashboard\n");
    if (count > 0) {
        msg_user("  %u template(s) on device\n", (unsigned)count);
    } else {
        msg_user("  No templates yet — enroll from web dashboard\n");
    }
    msg_user("\n");

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

    show_status_screen();
    app_buzzer_beep_ready();
    s_startup_done = true;
    vTaskDelete(NULL);
}

static void print_help(void)
{
    msg_user("\nCommands:\n");
    msg_user("  help           - this message\n");
    msg_user("  count          - templates stored in module\n");
    msg_user("  slots          - list occupied slots (NVS registry)\n");
    msg_user("  slots rebuild  - probe module and refresh slot registry\n");
    msg_user("  buttons        - show GO button GPIO level\n");
    msg_user("  oled           - scan I2C for OLED (debug wiring)\n");
    msg_user("  provision KEY  - save Supabase device API key (NVS)\n");
    msg_user("  provision clear - remove NVS key (use menuconfig key)\n");
    msg_user("  deviceid UUID  - save device UUID for Realtime (from web dashboard)\n");
    msg_user("  cloudurl URL   - set Supabase base URL (NVS)\n");
    msg_user("\nGPIO0 = GO (cloud command)   GPIO1 = bare enroll slot %u\n\n",
             (unsigned)APP_FP_BARE_SLOT);
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
    } else if (strcmp(cmd, "slots") == 0) {
        char *sub = strtok(NULL, " \t\r\n");
        if (sub != NULL && strcmp(sub, "rebuild") == 0) {
            if (!s_fp_ok) {
                msg_user("fingerprint module not ready\n");
                return;
            }
            msg_user("[SLOTS] Probing module (may take ~30s)…\n");
            s_busy = true;
            app_cloud_set_busy(true);
            if (app_fp_slots_rebuild_registry() == ESP_OK) {
                msg_user("[SLOTS] Registry rebuilt\n");
            } else {
                msg_user("[SLOTS] Rebuild failed\n");
            }
            s_busy = false;
            app_cloud_set_busy(false);
            return;
        }
        uint16_t list[APP_FP_MAX_SLOTS];
        size_t n = 0;
        app_fp_slots_list(list, APP_FP_MAX_SLOTS, &n);
        msg_user("Occupied slots (%u):", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            msg_user(" %u", (unsigned)list[i]);
        }
        msg_user("\n");
    } else if (strcmp(cmd, "oled") == 0) {
        app_oled_diag();
    } else if (strcmp(cmd, "provision") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (arg == NULL) {
            msg_user("usage: provision <device_api_key>  |  provision clear\n");
            return;
        }
        if (strcmp(arg, "clear") == 0) {
            esp_err_t err = app_cloud_clear_api_key();
            msg_user(err == ESP_OK ? "[CLOUD] NVS API key cleared\n" : "[CLOUD] clear failed: %s\n",
                     esp_err_to_name(err));
            return;
        }
        if (strlen(arg) < 16) {
            msg_user("usage: provision <device_api_key>\n");
            return;
        }
        esp_err_t err = app_cloud_provision(arg);
        if (err == ESP_OK) {
            msg_user("[CLOUD] API key saved to NVS\n");
            if (app_cloud_is_configured()) {
                app_cloud_set_sync_callback(on_cloud_sync, NULL);
                app_cloud_start_task(on_cloud_command, NULL);
            }
        } else {
            msg_user("[CLOUD] provision failed: %s\n", esp_err_to_name(err));
        }
    } else if (strcmp(cmd, "deviceid") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (arg == NULL || strlen(arg) < 8) {
            msg_user("usage: deviceid <device-uuid-from-web-dashboard>\n");
            return;
        }
        esp_err_t err = app_cloud_set_device_id(arg);
        if (err == ESP_OK) {
            app_realtime_refresh();
            msg_user("[CLOUD] device id saved — realtime reconnecting\n");
            if (app_wifi_is_connected()) {
                (void)app_realtime_start(on_cloud_command, NULL);
            }
        } else {
            msg_user("[CLOUD] deviceid failed: %s\n", esp_err_to_name(err));
        }
    } else if (strcmp(cmd, "cloudurl") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        if (arg == NULL) {
            msg_user("usage: cloudurl https://YOUR_REF.supabase.co\n");
            return;
        }
        esp_err_t err = app_cloud_set_url(arg);
        msg_user(err == ESP_OK ? "[CLOUD] URL saved\n" : "[CLOUD] URL save failed: %s\n",
                 esp_err_to_name(err));
    } else {
        msg_user("unknown: %s (try help)\n", cmd);
    }
}

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
        if (s_fp_ok && !attendance_paused()) {
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
                app_cloud_set_busy(true);
                handle_match(user_id);
                app_cloud_set_busy(false);
                s_busy = false;
            } else if (err == ESP_OK && app_fp_last_search_had_finger()) {
                s_busy = true;
                app_cloud_set_busy(true);
                handle_no_match();
                app_cloud_set_busy(false);
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

    memset(&s_pending_cmd, 0, sizeof(s_pending_cmd));

    s_action_q = xQueueCreate(1, sizeof(action_t));
    if (s_action_q == NULL) {
        ESP_LOGE(TAG, "action queue create failed");
        return;
    }

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

    app_buttons_init(on_go_button, on_bare_enroll_button, NULL);

    app_cloud_init();
    if (app_cloud_is_configured()) {
        app_cloud_set_sync_callback(on_cloud_sync, NULL);
        app_cloud_set_settings_callback(on_cloud_settings, NULL);
        if (app_cloud_start_task(on_cloud_command, NULL) != ESP_OK) {
            msg_user("[CLOUD] failed to start sync task\n");
        }
        if (app_cloud_device_id()[0] != '\0') {
            if (app_realtime_start(on_cloud_command, NULL) != ESP_OK) {
                msg_user("[REALTIME] not started — check cloud URL / publishable key\n");
            }
        } else {
            msg_user("[REALTIME] run: deviceid <uuid>  (shown in web dashboard)\n");
        }
    } else {
        msg_user("[CLOUD] not configured — use menuconfig or: provision / cloudurl\n");
    }

    xTaskCreate(ui_worker_task, "ui_worker", 4096, NULL, 5, NULL);
    xTaskCreate(startup_task, "startup", 4096, NULL, 5, NULL);
    xTaskCreate(serial_cmd_task, "serial_cmd", 4096, NULL, 4, NULL);
    xTaskCreate(attendance_task, "attendance", 4096, NULL, 3, NULL);
}
