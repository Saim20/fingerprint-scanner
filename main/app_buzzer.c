#include "app_buzzer.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUZZ_TAG "app_buzzer"

#define BUZZ_GPIO          10
#define BUZZ_LEDC_TIMER    LEDC_TIMER_0
#define BUZZ_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BUZZ_FREQ_HZ       2700
#define BUZZ_DUTY_ON       512

static bool s_ready;

static esp_err_t buzzer_tone_ms(uint32_t ms)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL, BUZZ_DUTY_ON);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL);
    return ESP_OK;
}

esp_err_t app_buzzer_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZ_LEDC_TIMER,
        .freq_hz = BUZZ_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t ch = {
        .gpio_num = BUZZ_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZ_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZ_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        return err;
    }

    s_ready = true;
    ESP_LOGI(BUZZ_TAG, "passive buzzer on GPIO%d", BUZZ_GPIO);
    return ESP_OK;
}

void app_buzzer_beep_ok(void)
{
    buzzer_tone_ms(120);
}

void app_buzzer_beep_deny(void)
{
    buzzer_tone_ms(80);
    vTaskDelay(pdMS_TO_TICKS(60));
    buzzer_tone_ms(80);
}
