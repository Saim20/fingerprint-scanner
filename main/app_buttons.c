#include "app_buttons.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BTN_TAG "app_buttons"

#define BTN_DEBOUNCE_MS       80
#define BTN_POLL_MS           10
#define BTN_LONG_PRESS_MS     1500
#define BTN_COOLDOWN_MS       800

typedef struct {
    gpio_num_t gpio;
    app_btn_id_t id;
    bool pressed;
    bool last_down;
    int stable_ms;
    int hold_ms;
    int cooldown_ms;
    bool long_fired;
} btn_state_t;

static const gpio_num_t s_btn_gpios[APP_BTN_COUNT] = {
    APP_BTN_PIN_ENROLL,
    APP_BTN_PIN_SCAN,
    APP_BTN_PIN_AUTO,
    APP_BTN_PIN_DELETE,
};

static const char *s_btn_names[APP_BTN_COUNT] = {
    "enroll", "scan", "auto", "delete",
};

static btn_state_t s_btns[APP_BTN_COUNT];
static app_btn_cb_t s_cb;
static void *s_cb_ctx;
static bool s_multi_down_warned;
static int s_global_cooldown_ms;

static bool btn_is_down(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 0;
}

static int btn_count_down(void)
{
    int n = 0;
    for (int i = 0; i < APP_BTN_COUNT; i++) {
        if (btn_is_down(s_btn_gpios[i])) {
            n++;
        }
    }
    return n;
}

static void btn_sync_state_no_events(void)
{
    for (int i = 0; i < APP_BTN_COUNT; i++) {
        bool down = btn_is_down(s_btns[i].gpio);
        s_btns[i].last_down = down;
        s_btns[i].pressed = down;
        s_btns[i].stable_ms = BTN_DEBOUNCE_MS;
        s_btns[i].hold_ms = 0;
        s_btns[i].long_fired = false;
    }
}

static void btn_fire(app_btn_id_t id, bool long_press)
{
    if (s_global_cooldown_ms > 0) {
        return;
    }
    ESP_LOGI(BTN_TAG, ">>> %s%s", s_btn_names[id], long_press ? " (long)" : "");
    s_btns[id].cooldown_ms = BTN_COOLDOWN_MS;
    s_global_cooldown_ms = BTN_COOLDOWN_MS;
    if (s_cb) {
        s_cb(id, long_press, s_cb_ctx);
    }
}

static void btn_poll_one(btn_state_t *b)
{
    if (s_global_cooldown_ms > 0) {
        s_global_cooldown_ms -= BTN_POLL_MS;
        if (s_global_cooldown_ms < 0) {
            s_global_cooldown_ms = 0;
        }
    }

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

    if (b->stable_ms < BTN_DEBOUNCE_MS) {
        return;
    }

    if (down && !b->pressed) {
        b->pressed = true;
        b->hold_ms = 0;
        b->long_fired = false;
        if (b->cooldown_ms == 0) {
            ESP_LOGI(BTN_TAG, "%s pressed (GPIO%d)", s_btn_names[b->id], (int)b->gpio);
        }
    } else if (!down && b->pressed) {
        b->pressed = false;
        if (b->cooldown_ms > 0 || s_global_cooldown_ms > 0) {
            return;
        }
        ESP_LOGI(BTN_TAG, "%s released (GPIO%d)", s_btn_names[b->id], (int)b->gpio);
        if (!b->long_fired) {
            btn_fire(b->id, false);
        }
    } else if (down && b->pressed) {
        b->hold_ms += BTN_POLL_MS;
        if (b->id == APP_BTN_DELETE && !b->long_fired && b->hold_ms >= BTN_LONG_PRESS_MS &&
            b->cooldown_ms == 0) {
            b->long_fired = true;
            btn_fire(APP_BTN_DELETE, true);
        }
    }
}

static void buttons_task(void *arg)
{
    (void)arg;
    for (;;) {
        int down_count = btn_count_down();

        if (down_count > 1) {
            if (!s_multi_down_warned) {
                ESP_LOGW(BTN_TAG, "%d GPIOs LOW — press one button at a time", down_count);
                s_multi_down_warned = true;
            }
        } else if (s_multi_down_warned) {
            ESP_LOGI(BTN_TAG, "single button press");
            s_multi_down_warned = false;
        }

        for (int i = 0; i < APP_BTN_COUNT; i++) {
            btn_poll_one(&s_btns[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

esp_err_t app_buttons_init(app_btn_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    s_multi_down_warned = false;
    s_global_cooldown_ms = 0;

    gpio_config_t io = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < APP_BTN_COUNT; i++) {
        io.pin_bit_mask |= (1ULL << s_btn_gpios[i]);
    }

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < APP_BTN_COUNT; i++) {
        s_btns[i].gpio = s_btn_gpios[i];
        s_btns[i].id = (app_btn_id_t)i;
        s_btns[i].pressed = false;
        s_btns[i].last_down = false;
        s_btns[i].stable_ms = 0;
        s_btns[i].hold_ms = 0;
        s_btns[i].cooldown_ms = 0;
        s_btns[i].long_fired = false;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    btn_sync_state_no_events();

    ESP_LOGI(BTN_TAG, "levels 1=open: GPIO%d=%d GPIO%d=%d GPIO%d=%d GPIO%d=%d",
             APP_BTN_PIN_ENROLL, gpio_get_level(APP_BTN_PIN_ENROLL),
             APP_BTN_PIN_SCAN, gpio_get_level(APP_BTN_PIN_SCAN),
             APP_BTN_PIN_AUTO, gpio_get_level(APP_BTN_PIN_AUTO),
             APP_BTN_PIN_DELETE, gpio_get_level(APP_BTN_PIN_DELETE));

    BaseType_t ok = xTaskCreate(buttons_task, "buttons", 2048, NULL, 6, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(BTN_TAG, "GPIO%d=enroll GPIO%d=scan GPIO%d=auto GPIO%d=delete",
             APP_BTN_PIN_ENROLL, APP_BTN_PIN_SCAN, APP_BTN_PIN_AUTO, APP_BTN_PIN_DELETE);
    return ESP_OK;
}

void app_buttons_print_levels(void)
{
    printf("Button GPIO (1=released 0=pressed) — press one, run 'buttons' again:\n");
    printf("  enroll GPIO%d = %d\n", APP_BTN_PIN_ENROLL, gpio_get_level(APP_BTN_PIN_ENROLL));
    printf("  scan   GPIO%d = %d\n", APP_BTN_PIN_SCAN, gpio_get_level(APP_BTN_PIN_SCAN));
    printf("  auto   GPIO%d = %d\n", APP_BTN_PIN_AUTO, gpio_get_level(APP_BTN_PIN_AUTO));
    printf("  delete GPIO%d = %d\n", APP_BTN_PIN_DELETE, gpio_get_level(APP_BTN_PIN_DELETE));
    fflush(stdout);
}
