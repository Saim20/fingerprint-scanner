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

static const gpio_num_t s_btn_gpios[] = {
    APP_BTN_PIN_GO,
    APP_BTN_PIN_UNUSED_1,
    APP_BTN_PIN_UNUSED_2,
    APP_BTN_PIN_UNUSED_3,
};

static gpio_num_t s_go_gpio = APP_BTN_PIN_GO;
static app_btn_go_cb_t s_cb;
static void *s_cb_ctx;
static bool s_pressed;
static bool s_last_down;
static int s_stable_ms;
static int s_cooldown_ms;

static bool btn_is_down(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 0;
}

static void btn_fire(void)
{
    if (s_cooldown_ms > 0) {
        return;
    }
    ESP_LOGI(BTN_TAG, ">>> GO");
    s_cooldown_ms = BTN_COOLDOWN_MS;
    if (s_cb) {
        s_cb(s_cb_ctx);
    }
}

static void buttons_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_cooldown_ms > 0) {
            s_cooldown_ms -= BTN_POLL_MS;
            if (s_cooldown_ms < 0) {
                s_cooldown_ms = 0;
            }
        }

        bool down = btn_is_down(s_go_gpio);

        if (down != s_last_down) {
            s_stable_ms = 0;
            s_last_down = down;
        } else if (s_stable_ms < BTN_DEBOUNCE_MS) {
            s_stable_ms += BTN_POLL_MS;
        }

        if (s_stable_ms >= BTN_DEBOUNCE_MS) {
            if (down && !s_pressed) {
                s_pressed = true;
                if (s_cooldown_ms == 0) {
                    ESP_LOGI(BTN_TAG, "GO pressed (GPIO%d)", (int)s_go_gpio);
                }
            } else if (!down && s_pressed) {
                s_pressed = false;
                if (s_cooldown_ms == 0) {
                    ESP_LOGI(BTN_TAG, "GO released (GPIO%d)", (int)s_go_gpio);
                    btn_fire();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

esp_err_t app_buttons_init(app_btn_go_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    s_cooldown_ms = 0;
    s_pressed = false;
    s_last_down = false;
    s_stable_ms = BTN_DEBOUNCE_MS;

    gpio_config_t io = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (size_t i = 0; i < sizeof(s_btn_gpios) / sizeof(s_btn_gpios[0]); i++) {
        io.pin_bit_mask |= (1ULL << s_btn_gpios[i]);
    }

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    s_last_down = btn_is_down(s_go_gpio);
    s_pressed = s_last_down;

    ESP_LOGI(BTN_TAG, "GPIO%d=GO (run cloud cmd)", APP_BTN_PIN_GO);

    BaseType_t ok = xTaskCreate(buttons_task, "buttons", 2048, NULL, 6, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_buttons_print_levels(void)
{
    printf("Button GPIO (1=released 0=pressed):\n");
    printf("  GO     GPIO%d = %d\n", APP_BTN_PIN_GO, gpio_get_level(APP_BTN_PIN_GO));
    printf("  (unused GPIO1–3)\n");
    fflush(stdout);
}
