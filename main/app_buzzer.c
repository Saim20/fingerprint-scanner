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

static esp_err_t buzzer_tone_ms(uint32_t freq_hz, uint32_t ms)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (freq_hz > 0) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_TIMER, freq_hz);
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL, BUZZ_DUTY_ON);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL);
    return ESP_OK;
}

static void buzzer_gap_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
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
    buzzer_tone_ms(BUZZ_FREQ_HZ, 120);
}

void app_buzzer_beep_deny(void)
{
    buzzer_tone_ms(2200, 80);
    buzzer_gap_ms(60);
    buzzer_tone_ms(2200, 80);
}

void app_buzzer_beep_notify(void)
{
    buzzer_tone_ms(3200, 70);
}

void app_buzzer_beep_cancel(void)
{
    buzzer_tone_ms(1800, 60);
}

void app_buzzer_beep_mode(bool passive_scan)
{
    if (passive_scan) {
        buzzer_tone_ms(2200, 55);
        buzzer_gap_ms(45);
        buzzer_tone_ms(3000, 75);
    } else {
        buzzer_tone_ms(3000, 55);
        buzzer_gap_ms(45);
        buzzer_tone_ms(2200, 75);
    }
}

void app_buzzer_beep_ready(void)
{
    buzzer_tone_ms(2000, 45);
    buzzer_gap_ms(35);
    buzzer_tone_ms(2600, 45);
    buzzer_gap_ms(35);
    buzzer_tone_ms(3200, 70);
}

void app_buzzer_beep_prompt(void)
{
    buzzer_tone_ms(2400, 45);
    buzzer_gap_ms(30);
    buzzer_tone_ms(3000, 55);
}

void app_buzzer_beep_lift(void)
{
    buzzer_tone_ms(3000, 40);
    buzzer_gap_ms(30);
    buzzer_tone_ms(2400, 50);
}

void app_buzzer_beep_no_match(void)
{
    buzzer_tone_ms(1500, 100);
}

void app_buzzer_beep_go(void)
{
    buzzer_tone_ms(3200, 40);
    buzzer_gap_ms(35);
    buzzer_tone_ms(3200, 40);
}

void app_buzzer_beep_busy(void)
{
    buzzer_tone_ms(2500, 35);
}

void app_buzzer_beep_start(void)
{
    buzzer_tone_ms(2600, 90);
}

void app_buzzer_beep_done(void)
{
    buzzer_tone_ms(2400, 55);
    buzzer_gap_ms(40);
    buzzer_tone_ms(2800, 55);
    buzzer_gap_ms(40);
    buzzer_tone_ms(3200, 80);
}

void app_buzzer_beep_warn(void)
{
    buzzer_tone_ms(2000, 55);
    buzzer_gap_ms(50);
    buzzer_tone_ms(2000, 55);
}

void app_buzzer_beep_error(void)
{
    buzzer_tone_ms(1600, 180);
    buzzer_gap_ms(70);
    buzzer_tone_ms(1600, 180);
    buzzer_gap_ms(70);
    buzzer_tone_ms(1600, 260);
}

void app_buzzer_beep_wifi(bool connected)
{
    if (connected) {
        buzzer_tone_ms(2800, 50);
        buzzer_gap_ms(40);
        buzzer_tone_ms(3400, 60);
    } else {
        buzzer_tone_ms(1800, 130);
    }
}
