#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t app_buzzer_init(void);

/** Single tone — step OK, match, delete OK. */
void app_buzzer_beep_ok(void);

/** Double low tone — failure, rejected action. */
void app_buzzer_beep_deny(void);

/** Short high pip — cloud command received. */
void app_buzzer_beep_notify(void);

/** Short low pip — command cancelled. */
void app_buzzer_beep_cancel(void);

/** Two-tone sweep — passive scan toggled. */
void app_buzzer_beep_mode(bool passive_scan);

/** Ascending chime — device ready after boot. */
void app_buzzer_beep_ready(void);

/** Rising tones — place finger / action needed now. */
void app_buzzer_beep_prompt(void);

/** Falling tones — lift finger off sensor. */
void app_buzzer_beep_lift(void);

/** Single low tone — finger read but not recognized. */
void app_buzzer_beep_no_match(void);

/** Double high pip — GO button accepted. */
void app_buzzer_beep_go(void);

/** Very short blip — busy, ignored input. */
void app_buzzer_beep_busy(void);

/** Medium tone — operation starting. */
void app_buzzer_beep_start(void);

/** Triple ascending — major task finished (enroll complete). */
void app_buzzer_beep_done(void);

/** Double medium — warning (empty slot, no templates, drift). */
void app_buzzer_beep_warn(void);

/** Long low triple — hardware / boot error. */
void app_buzzer_beep_error(void);

/** WiFi link up (double high) or down (long low). */
void app_buzzer_beep_wifi(bool connected);
