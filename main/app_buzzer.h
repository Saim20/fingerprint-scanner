#pragma once

#include "esp_err.h"

esp_err_t app_buzzer_init(void);
void app_buzzer_beep_ok(void);
void app_buzzer_beep_deny(void);
