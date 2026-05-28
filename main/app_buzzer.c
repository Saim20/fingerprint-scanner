#include "app_buzzer.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BUZZ_TAG "app_buzzer"

#define BUZZ_GPIO          10
#define BUZZ_LEDC_TIMER    LEDC_TIMER_0
#define BUZZ_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BUZZ_FREQ_HZ       2700
#define BUZZ_DUTY_ON       512

static bool s_ready;
static SemaphoreHandle_t s_lock;

static void buzzer_stop(void)
{
    if (!s_ready) {
        return;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL);
}

static void buzzer_tone_ms(uint32_t freq_hz, uint32_t ms)
{
    if (!s_ready) {
        return;
    }
    buzzer_stop();
    if (freq_hz > 0) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_TIMER, freq_hz);
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL, BUZZ_DUTY_ON);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    buzzer_stop();
}

static void buzzer_gap_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void buzzer_with_lock(void (*play)(void))
{
    if (!s_ready || play == NULL || s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return;
    }
    play();
    xSemaphoreGive(s_lock);
}

#define BUZZER_FN(name, body) \
    static void name##_play(void) body \
    void name(void) { buzzer_with_lock(name##_play); }

esp_err_t app_buzzer_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
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

BUZZER_FN(app_buzzer_beep_ok, { buzzer_tone_ms(BUZZ_FREQ_HZ, 120); })

BUZZER_FN(app_buzzer_beep_deny,
          {
              buzzer_tone_ms(2200, 80);
              buzzer_gap_ms(60);
              buzzer_tone_ms(2200, 80);
          })

BUZZER_FN(app_buzzer_beep_notify, { buzzer_tone_ms(3200, 70); })

BUZZER_FN(app_buzzer_beep_cancel, { buzzer_tone_ms(1800, 60); })

BUZZER_FN(app_buzzer_beep_command_mode,
          {
              buzzer_tone_ms(3000, 55);
              buzzer_gap_ms(45);
              buzzer_tone_ms(2200, 75);
          })

BUZZER_FN(app_buzzer_beep_ready,
          {
              buzzer_tone_ms(2000, 45);
              buzzer_gap_ms(35);
              buzzer_tone_ms(2600, 45);
              buzzer_gap_ms(35);
              buzzer_tone_ms(3200, 70);
          })

BUZZER_FN(app_buzzer_beep_prompt,
          {
              buzzer_tone_ms(2400, 45);
              buzzer_gap_ms(30);
              buzzer_tone_ms(3000, 55);
          })

BUZZER_FN(app_buzzer_beep_attendance_ok,
          {
              buzzer_tone_ms(3000, 35);
              buzzer_gap_ms(25);
              buzzer_tone_ms(3600, 45);
          })

BUZZER_FN(app_buzzer_beep_attendance_unknown, { buzzer_tone_ms(2000, 40); })

BUZZER_FN(app_buzzer_beep_go,
          {
              buzzer_tone_ms(3200, 40);
              buzzer_gap_ms(35);
              buzzer_tone_ms(3200, 40);
          })

BUZZER_FN(app_buzzer_beep_busy, { buzzer_tone_ms(2500, 35); })

BUZZER_FN(app_buzzer_beep_start, { buzzer_tone_ms(2600, 90); })

BUZZER_FN(app_buzzer_beep_done,
          {
              buzzer_tone_ms(2400, 55);
              buzzer_gap_ms(40);
              buzzer_tone_ms(2800, 55);
              buzzer_gap_ms(40);
              buzzer_tone_ms(3200, 80);
          })

BUZZER_FN(app_buzzer_beep_warn,
          {
              buzzer_tone_ms(2000, 55);
              buzzer_gap_ms(50);
              buzzer_tone_ms(2000, 55);
          })

BUZZER_FN(app_buzzer_beep_error,
          {
              buzzer_tone_ms(1600, 180);
              buzzer_gap_ms(70);
              buzzer_tone_ms(1600, 180);
              buzzer_gap_ms(70);
              buzzer_tone_ms(1600, 260);
          })

static bool s_wifi_connected;

static void app_buzzer_beep_wifi_play(void)
{
    if (s_wifi_connected) {
        buzzer_tone_ms(2800, 50);
        buzzer_gap_ms(40);
        buzzer_tone_ms(3400, 60);
    } else {
        buzzer_tone_ms(1800, 130);
    }
}

void app_buzzer_beep_wifi(bool connected)
{
    s_wifi_connected = connected;
    buzzer_with_lock(app_buzzer_beep_wifi_play);
}
