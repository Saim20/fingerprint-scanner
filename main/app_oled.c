#include "app_oled.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "u8g2.h"
#include "esp32_hw_i2c.h"
#define OLED_TAG "app_oled"

#define OLED_I2C_PORT           0
#define OLED_PIN_SDA            APP_OLED_PIN_SDA
#define OLED_PIN_SCL            APP_OLED_PIN_SCL
#define OLED_I2C_ADDR_7BIT      APP_OLED_I2C_ADDR_7BIT
#define OLED_I2C_HZ             100000
#define OLED_PROBE_TIMEOUT_MS   100

static u8g2_t s_u8g2;
static u8g2_esp32_i2c_ctx_t s_i2c_ctx;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static esp_err_t oled_bus_create(int sda_gpio, int scl_gpio, i2c_master_bus_handle_t *bus_out)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
        .flags.allow_pd = 0,
    };
    return i2c_new_master_bus(&bus_cfg, bus_out);
}

static esp_err_t oled_probe_on_bus(int sda_gpio, int scl_gpio, uint8_t addr_7bit)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = oled_bus_create(sda_gpio, scl_gpio, &bus);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_probe(bus, addr_7bit, OLED_PROBE_TIMEOUT_MS);
    i2c_del_master_bus(bus);
    return err;
}

static esp_err_t oled_find_device(uint8_t *addr_out, int *sda_out, int *scl_out)
{
    static const uint8_t addrs[] = { 0x3c, 0x3d };
    static const int pin_pairs[][2] = {
        { OLED_PIN_SDA, OLED_PIN_SCL },
        { OLED_PIN_SCL, OLED_PIN_SDA },
    };

    for (int p = 0; p < 2; p++) {
        int sda = pin_pairs[p][0];
        int scl = pin_pairs[p][1];
        for (size_t a = 0; a < sizeof(addrs); a++) {
            esp_err_t err = oled_probe_on_bus(sda, scl, addrs[a]);
            if (err == ESP_OK) {
                *addr_out = addrs[a];
                *sda_out = sda;
                *scl_out = scl;
                if (p == 1) {
                    ESP_LOGW(OLED_TAG, "OLED responds with SDA/SCL swapped — swap dupont wires");
                }
                if (addrs[a] != OLED_I2C_ADDR_7BIT) {
                    ESP_LOGW(OLED_TAG, "OLED at 0x%02X — move ADDRESS_SELECT jumper to 0x78",
                              addrs[a]);
                }
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void oled_log_wiring_help(void)
{
    ESP_LOGW(OLED_TAG, "No OLED on I2C — set APP_OLED_ENABLE 0 to skip, or fix wiring:");
    ESP_LOGW(OLED_TAG, "  SDA=GPIO%d SCL=GPIO%d, VCC=3V3 GND=GND", OLED_PIN_SDA, OLED_PIN_SCL);
}

esp_err_t app_oled_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

#if !APP_OLED_ENABLE
    ESP_LOGI(OLED_TAG, "OLED disabled (APP_OLED_ENABLE=0) — fingerprint/serial still run");
    return ESP_ERR_NOT_FOUND;
#endif

    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t addr = OLED_I2C_ADDR_7BIT;
    int sda = OLED_PIN_SDA;
    int scl = OLED_PIN_SCL;
    esp_err_t err = oled_find_device(&addr, &sda, &scl);
    if (err != ESP_OK) {
        oled_log_wiring_help();
        return err;
    }

    memset(&s_i2c_ctx, 0, sizeof(s_i2c_ctx));
    s_i2c_ctx.cfg.i2c_port = OLED_I2C_PORT;
    s_i2c_ctx.cfg.sda_pin = sda;
    s_i2c_ctx.cfg.scl_pin = scl;
    s_i2c_ctx.cfg.clk_hz = OLED_I2C_HZ;
    s_i2c_ctx.cfg.dev_addr_7bit = addr;
    s_i2c_ctx.cfg.timeout_ms = 1000;
    s_i2c_ctx.cfg.reset_pin = U8G2_ESP32_PIN_UNUSED;

    err = u8g2_esp32_i2c_set_default_context(&s_i2c_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(OLED_TAG, "u8g2_esp32_i2c_set_default_context failed: %s", esp_err_to_name(err));
        return err;
    }

    /* SSD1306 128x64 I2C — works on most 1.3" SH1106 modules */
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&s_u8g2, U8G2_R0,
                                           u8x8_byte_esp32_hw_i2c,
                                           u8x8_gpio_and_delay_esp32_i2c);
    u8x8_SetI2CAddress(u8g2_GetU8x8(&s_u8g2), (uint8_t)(addr << 1));

    u8g2_InitDisplay(&s_u8g2);
    u8g2_ClearDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(OLED_TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(OLED_TAG, "OLED OK (SDA=GPIO%d SCL=GPIO%d addr=0x%02x)", sda, scl, addr);
    return ESP_OK;
}

bool app_oled_is_ready(void)
{
    return s_ready;
}

void app_oled_show_lines(const char *line1, const char *line2, const char *line3, const char *line4)
{
    if (!s_ready || s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return;
    }

    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);

    const int line_h = 12;
    int y = line_h;
    if (line1) {
        u8g2_DrawStr(&s_u8g2, 0, y, line1);
        y += line_h;
    }
    if (line2) {
        u8g2_DrawStr(&s_u8g2, 0, y, line2);
        y += line_h;
    }
    if (line3) {
        u8g2_DrawStr(&s_u8g2, 0, y, line3);
        y += line_h;
    }
    if (line4) {
        u8g2_DrawStr(&s_u8g2, 0, y, line4);
    }

    u8g2_SendBuffer(&s_u8g2);
    xSemaphoreGive(s_lock);
}
