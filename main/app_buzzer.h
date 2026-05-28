#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t app_buzzer_init(void);

void app_buzzer_beep_ok(void);
void app_buzzer_beep_deny(void);
void app_buzzer_beep_notify(void);
void app_buzzer_beep_cancel(void);

/** Two-tone sweep — leaving command mode (passive scan off). */
void app_buzzer_beep_command_mode(void);

void app_buzzer_beep_ready(void);
void app_buzzer_beep_prompt(void);
void app_buzzer_beep_attendance_ok(void);
void app_buzzer_beep_attendance_unknown(void);
void app_buzzer_beep_go(void);
void app_buzzer_beep_busy(void);
void app_buzzer_beep_start(void);
void app_buzzer_beep_done(void);
void app_buzzer_beep_warn(void);
void app_buzzer_beep_error(void);
void app_buzzer_beep_wifi(bool connected);
