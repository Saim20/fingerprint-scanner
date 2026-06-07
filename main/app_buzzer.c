#include "app_buzzer.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUZZ_TAG "app_buzzer"

#define BUZZ_GPIO          10
#define BUZZ_LEDC_TIMER    LEDC_TIMER_0
#define BUZZ_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BUZZ_FREQ_HZ       2700
#define BUZZ_DUTY_ON       512

typedef enum {
    BUZZ_EVT_OK,
    BUZZ_EVT_DENY,
    BUZZ_EVT_NOTIFY,
    BUZZ_EVT_CANCEL,
    BUZZ_EVT_COMMAND_MODE,
    BUZZ_EVT_READY,
    BUZZ_EVT_PROMPT,
    BUZZ_EVT_ATTENDANCE_OK,
    BUZZ_EVT_ATTENDANCE_UNKNOWN,
    BUZZ_EVT_GO,
    BUZZ_EVT_BUSY,
    BUZZ_EVT_START,
    BUZZ_EVT_DONE,
    BUZZ_EVT_WARN,
    BUZZ_EVT_ERROR,
    BUZZ_EVT_WIFI_CONNECTED,
    BUZZ_EVT_WIFI_DISCONNECTED,
} buzz_evt_t;

static bool s_ready;
static QueueHandle_t s_q;

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
    if (!s_ready || ms == 0) {
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
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void buzzer_play(buzz_evt_t evt)
{
    switch (evt) {
    case BUZZ_EVT_OK:
        buzzer_tone_ms(BUZZ_FREQ_HZ, 120);
        break;
    case BUZZ_EVT_DENY:
        buzzer_tone_ms(2200, 80);
        buzzer_gap_ms(60);
        buzzer_tone_ms(2200, 80);
        break;
    case BUZZ_EVT_NOTIFY:
        buzzer_tone_ms(3200, 70);
        break;
    case BUZZ_EVT_CANCEL:
        buzzer_tone_ms(1800, 60);
        break;
    case BUZZ_EVT_COMMAND_MODE:
        buzzer_tone_ms(3000, 55);
        buzzer_gap_ms(45);
        buzzer_tone_ms(2200, 75);
        break;
    case BUZZ_EVT_READY:
        buzzer_tone_ms(2000, 45);
        buzzer_gap_ms(35);
        buzzer_tone_ms(2600, 45);
        buzzer_gap_ms(35);
        buzzer_tone_ms(3200, 70);
        break;
    case BUZZ_EVT_PROMPT:
        buzzer_tone_ms(2400, 45);
        buzzer_gap_ms(30);
        buzzer_tone_ms(3000, 55);
        break;
    case BUZZ_EVT_ATTENDANCE_OK:
        buzzer_tone_ms(3200, 30);
        buzzer_gap_ms(20);
        buzzer_tone_ms(3600, 35);
        break;
    case BUZZ_EVT_ATTENDANCE_UNKNOWN:
        buzzer_tone_ms(2000, 30);
        break;
    case BUZZ_EVT_GO:
        buzzer_tone_ms(3200, 40);
        buzzer_gap_ms(35);
        buzzer_tone_ms(3200, 40);
        break;
    case BUZZ_EVT_BUSY:
        buzzer_tone_ms(2500, 35);
        break;
    case BUZZ_EVT_START:
        buzzer_tone_ms(2600, 90);
        break;
    case BUZZ_EVT_DONE:
        buzzer_tone_ms(2400, 55);
        buzzer_gap_ms(40);
        buzzer_tone_ms(2800, 55);
        buzzer_gap_ms(40);
        buzzer_tone_ms(3200, 80);
        break;
    case BUZZ_EVT_WARN:
        buzzer_tone_ms(2000, 55);
        buzzer_gap_ms(50);
        buzzer_tone_ms(2000, 55);
        break;
    case BUZZ_EVT_ERROR:
        buzzer_tone_ms(1600, 180);
        buzzer_gap_ms(70);
        buzzer_tone_ms(1600, 180);
        buzzer_gap_ms(70);
        buzzer_tone_ms(1600, 260);
        break;
    case BUZZ_EVT_WIFI_CONNECTED:
        buzzer_tone_ms(2800, 50);
        buzzer_gap_ms(40);
        buzzer_tone_ms(3400, 60);
        break;
    case BUZZ_EVT_WIFI_DISCONNECTED:
        buzzer_tone_ms(1800, 130);
        break;
    default:
        break;
    }
    buzzer_stop();
}

static void buzzer_task(void *arg)
{
    (void)arg;
    buzz_evt_t evt;

    for (;;) {
        if (xQueueReceive(s_q, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        buzzer_play(evt);
    }
}

static void buzzer_post(buzz_evt_t evt)
{
    if (s_q == NULL) {
        return;
    }
    (void)xQueueOverwrite(s_q, &evt);
}

esp_err_t app_buzzer_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    if (s_q == NULL) {
        s_q = xQueueCreate(1, sizeof(buzz_evt_t));
        if (s_q == NULL) {
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

    static bool task_started;
    if (!task_started) {
        BaseType_t ok = xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 4, NULL);
        if (ok != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
        task_started = true;
    }

    s_ready = true;
    ESP_LOGI(BUZZ_TAG, "passive buzzer on GPIO%d", BUZZ_GPIO);
    return ESP_OK;
}

void app_buzzer_beep_ok(void) { buzzer_post(BUZZ_EVT_OK); }
void app_buzzer_beep_deny(void) { buzzer_post(BUZZ_EVT_DENY); }
void app_buzzer_beep_notify(void) { buzzer_post(BUZZ_EVT_NOTIFY); }
void app_buzzer_beep_cancel(void) { buzzer_post(BUZZ_EVT_CANCEL); }
void app_buzzer_beep_command_mode(void) { buzzer_post(BUZZ_EVT_COMMAND_MODE); }
void app_buzzer_beep_ready(void) { buzzer_post(BUZZ_EVT_READY); }
void app_buzzer_beep_prompt(void) { buzzer_post(BUZZ_EVT_PROMPT); }
void app_buzzer_beep_attendance_ok(void) { buzzer_post(BUZZ_EVT_ATTENDANCE_OK); }
void app_buzzer_beep_attendance_unknown(void) { buzzer_post(BUZZ_EVT_ATTENDANCE_UNKNOWN); }
void app_buzzer_beep_go(void) { buzzer_post(BUZZ_EVT_GO); }
void app_buzzer_beep_busy(void) { buzzer_post(BUZZ_EVT_BUSY); }
void app_buzzer_beep_start(void) { buzzer_post(BUZZ_EVT_START); }
void app_buzzer_beep_done(void) { buzzer_post(BUZZ_EVT_DONE); }
void app_buzzer_beep_warn(void) { buzzer_post(BUZZ_EVT_WARN); }
void app_buzzer_beep_error(void) { buzzer_post(BUZZ_EVT_ERROR); }

void app_buzzer_beep_wifi(bool connected)
{
    buzzer_post(connected ? BUZZ_EVT_WIFI_CONNECTED : BUZZ_EVT_WIFI_DISCONNECTED);
}
