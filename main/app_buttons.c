#include "app_buttons.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BTN_TAG "app_buttons"

#define BTN_DEBOUNCE_MS   80
#define BTN_POLL_MS       10
#define BTN_COOLDOWN_MS   800

typedef struct {
    gpio_num_t gpio;
    const char *label;
    app_btn_cb_t on_release;
    bool pressed;
    bool last_down;
    int stable_ms;
    int cooldown_ms;
} btn_slot_t;

static btn_slot_t s_btn;
static void *s_ctx;

static bool btn_is_down(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 0;
}

static void btn_fire(btn_slot_t *b)
{
    if (b->cooldown_ms > 0 || b->on_release == NULL) {
        return;
    }
    ESP_LOGI(BTN_TAG, ">>> %s", b->label);
    b->cooldown_ms = BTN_COOLDOWN_MS;
    b->on_release(s_ctx);
}

static void buttons_task(void *arg)
{
    (void)arg;

    for (;;) {
        btn_slot_t *b = &s_btn;
        if (b->cooldown_ms > 0) {
            b->cooldown_ms -= BTN_POLL_MS;
            if (b->cooldown_ms < 0) {
                b->cooldown_ms = 0;
            }
        }

        bool down = btn_is_down(b->gpio);

        if (down != b->last_down) {
            b->stable_ms = 0;
            b->last_down = down;
        } else if (b->stable_ms < BTN_DEBOUNCE_MS) {
            b->stable_ms += BTN_POLL_MS;
        }

        if (b->stable_ms >= BTN_DEBOUNCE_MS) {
            if (down && !b->pressed) {
                b->pressed = true;
                if (b->cooldown_ms == 0) {
                    ESP_LOGI(BTN_TAG, "%s pressed (GPIO%d)", b->label, (int)b->gpio);
                }
            } else if (!down && b->pressed) {
                b->pressed = false;
                if (b->cooldown_ms == 0) {
                    ESP_LOGI(BTN_TAG, "%s released (GPIO%d)", b->label, (int)b->gpio);
                    btn_fire(b);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

esp_err_t app_buttons_init(app_btn_cb_t go_cb, void *ctx)
{
    s_ctx = ctx;
    s_btn = (btn_slot_t){
        .gpio = APP_BTN_PIN_GO,
        .label = "GO",
        .on_release = go_cb,
        .pressed = false,
        .last_down = false,
        .stable_ms = BTN_DEBOUNCE_MS,
        .cooldown_ms = 0,
    };

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << APP_BTN_PIN_GO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    s_btn.last_down = btn_is_down(s_btn.gpio);
    s_btn.pressed = s_btn.last_down;

    ESP_LOGI(BTN_TAG, "GPIO%d=GO (cloud command)", APP_BTN_PIN_GO);

    BaseType_t ok = xTaskCreate(buttons_task, "buttons", 2048, NULL, 6, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_buttons_print_levels(void)
{
    printf("Button GPIO (1=released 0=pressed):\n");
    printf("  GO GPIO%d = %d\n", APP_BTN_PIN_GO, gpio_get_level(APP_BTN_PIN_GO));
    fflush(stdout);
}
