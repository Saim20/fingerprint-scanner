#include "app_oled.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_oled_font.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_sh1106.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define OLED_TAG "app_oled"

#define OLED_W                  128
#define OLED_H                  64
#define OLED_I2C_PORT           0
#define OLED_I2C_HZ             100000
#define OLED_PROBE_MS           250
#define OLED_LINE_H             8

static i2c_master_bus_handle_t s_i2c_bus;
static int s_sda_pin = APP_OLED_PIN_SDA;
static int s_scl_pin = APP_OLED_PIN_SCL;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint8_t s_fb[OLED_W * OLED_H / 8];
static SemaphoreHandle_t s_lock;
static bool s_ready;

typedef struct {
    int sda;
    int scl;
} oled_pin_pair_t;

/** Pairs to try if default pins have no ACK (external OLED wired elsewhere). */
static const oled_pin_pair_t s_pin_candidates[] = {
    {APP_OLED_PIN_SDA, APP_OLED_PIN_SCL},
    {APP_OLED_PIN_SCL, APP_OLED_PIN_SDA},
    {5, 6},
    {6, 5},
    {8, 9},
    {9, 8},
    {2, 3},
    {3, 2},
};

static void oled_pin_pullups(int sda_gpio, int scl_gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << sda_gpio) | (1ULL << scl_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&cfg);
}

static void oled_print_line_levels(int sda_gpio, int scl_gpio)
{
    oled_pin_pullups(sda_gpio, scl_gpio);
    vTaskDelay(pdMS_TO_TICKS(5));
    int sda = gpio_get_level(sda_gpio);
    int scl = gpio_get_level(scl_gpio);
    printf("  GPIO%d(SDA)=%d GPIO%d(SCL)=%d (both should be 1; 0=shorted/missing pull-up)\n",
           sda_gpio, sda, scl_gpio, scl);
    if (sda == 0 || scl == 0) {
        printf("  -> Add 4.7k from SDA to 3V3 and SCL to 3V3, check shorts to GND\n");
    }
}

static void oled_bus_delete(void)
{
    if (s_i2c_bus != NULL) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

static esp_err_t oled_bus_create(int sda_gpio, int scl_gpio)
{
    oled_bus_delete();
    oled_pin_pullups(sda_gpio, scl_gpio);

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
    return i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
}

/*
 * SH1106/SSD1306 modules often print 0x78 / 0x7A on the PCB — those are 8-bit write
 * addresses. ESP-IDF uses 7-bit addresses: 0x78>>1 = 0x3C, 0x7A>>1 = 0x3D.
 * Do not pass 0x78 into i2c_master_probe; it will not match a 0x78-silkscreen OLED.
 */
static const uint8_t s_oled_addrs_7bit[] = {0x3c, 0x3d};

static uint8_t oled_probe_addrs_on_bus(void)
{
    esp_log_level_t prev = esp_log_level_get("i2c.master");

    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    for (size_t i = 0; i < sizeof(s_oled_addrs_7bit); i++) {
        if (i2c_master_probe(s_i2c_bus, s_oled_addrs_7bit[i], OLED_PROBE_MS) == ESP_OK) {
            esp_log_level_set("i2c.master", prev);
            return s_oled_addrs_7bit[i];
        }
    }
    esp_log_level_set("i2c.master", prev);
    return 0;
}

static uint8_t oled_probe_addrs(void)
{
    return oled_probe_addrs_on_bus();
}

static void oled_print_addr_help(void)
{
    printf("  I2C 7-bit addrs probed: 0x3C (PCB often \"0x78\"), 0x3D (PCB often \"0x7A\")\n");
    printf("  Silkscreen 0x78/0x7A are NOT used directly — firmware already maps them.\n");
}

static void oled_print_hw_checklist(void)
{
    printf("  3.3V on VCC but no ACK usually means:\n");
    printf("    - SDA/SCL not actually reaching the OLED chip (wrong header pin)\n");
    printf("    - No I2C pull-ups (add 4.7k SDA->3V3 and SCL->3V3)\n");
    printf("    - SPI-only module (4-pin looks like I2C but is not)\n");
    printf("    - 5V I2C module fed 3.3V (try module VCC=5V from boost, level-safe data)\n");
    printf("    - GND not common between ESP and display\n");
}

static void oled_scan_bus(int sda_gpio, int scl_gpio)
{
    if (oled_bus_create(sda_gpio, scl_gpio) != ESP_OK) {
        return;
    }
    esp_log_level_t prev = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    bool any = false;
    printf("  GPIO%d/GPIO%d bus scan (7-bit):", sda_gpio, scl_gpio);
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, OLED_PROBE_MS) == ESP_OK) {
            printf(" 0x%02X", addr);
            any = true;
        }
    }
    printf(any ? "\n" : " (none)\n");
    esp_log_level_set("i2c.master", prev);
    oled_bus_delete();
}

typedef struct {
    int sda;
    int scl;
    uint8_t addr;
} oled_bus_match_t;

static bool oled_try_pins(int sda_gpio, int scl_gpio, oled_bus_match_t *out)
{
    if (oled_bus_create(sda_gpio, scl_gpio) != ESP_OK) {
        return false;
    }
    uint8_t addr = oled_probe_addrs();
    if (addr == 0) {
        return false;
    }
    out->sda = sda_gpio;
    out->scl = scl_gpio;
    out->addr = addr;
    return true;
}

static bool oled_find_bus(oled_bus_match_t *out)
{
    for (size_t i = 0; i < sizeof(s_pin_candidates) / sizeof(s_pin_candidates[0]); i++) {
        const oled_pin_pair_t *p = &s_pin_candidates[i];
        ESP_LOGI(OLED_TAG, "probe SDA=GPIO%d SCL=GPIO%d ...", p->sda, p->scl);
        if (oled_try_pins(p->sda, p->scl, out)) {
            if (p->sda != APP_OLED_PIN_SDA || p->scl != APP_OLED_PIN_SCL) {
                ESP_LOGW(OLED_TAG, "OLED not on GPIO%d/GPIO%d — found on GPIO%d/GPIO%d",
                         APP_OLED_PIN_SDA, APP_OLED_PIN_SCL, out->sda, out->scl);
            } else if (p->sda == APP_OLED_PIN_SCL) {
                ESP_LOGW(OLED_TAG, "OLED found with SDA/SCL swapped — swap wires");
            }
            return true;
        }
    }
    return false;
}

void app_oled_diag(void)
{
    printf("\n[OLED] I2C diagnostic (7-bit 0x3C/0x3D = silkscreen 0x78/0x7A)\n");
    oled_print_addr_help();
    ESP_LOGI(OLED_TAG, "I2C diagnostic");

    bool any = false;
    esp_log_level_t prev = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    for (size_t i = 0; i < sizeof(s_pin_candidates) / sizeof(s_pin_candidates[0]); i++) {
        const oled_pin_pair_t *p = &s_pin_candidates[i];
        if (oled_bus_create(p->sda, p->scl) != ESP_OK) {
            printf("  GPIO%d/GPIO%d: bus create failed\n", p->sda, p->scl);
            continue;
        }
        uint8_t addr = oled_probe_addrs();
        if (addr != 0) {
            printf("  GPIO%d/GPIO%d -> found 0x%02X\n", p->sda, p->scl, addr);
            ESP_LOGI(OLED_TAG, "found 0x%02X on SDA=%d SCL=%d", addr, p->sda, p->scl);
            any = true;
        }
    }
    esp_log_level_set("i2c.master", prev);
    oled_bus_delete();

    if (!any) {
        printf("  No OLED on any tried pin pair.\n");
        oled_print_addr_help();
        oled_print_hw_checklist();
        printf("  Target: SDA=GPIO%d SCL=GPIO%d\n", APP_OLED_PIN_SDA, APP_OLED_PIN_SCL);
        oled_print_line_levels(APP_OLED_PIN_SDA, APP_OLED_PIN_SCL);
        printf("  Full bus scan on target pins:\n");
        oled_scan_bus(APP_OLED_PIN_SDA, APP_OLED_PIN_SCL);
        oled_scan_bus(APP_OLED_PIN_SCL, APP_OLED_PIN_SDA);
        ESP_LOGW(OLED_TAG, "no device on any candidate pins");
    } else if (!s_ready) {
        printf("  Reboot after fixing wires, or set APP_OLED_PIN_* in app_oled.h\n");
    }
    printf("\n");
}

static esp_err_t oled_panel_init(uint8_t i2c_addr_7bit)
{
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = i2c_addr_7bit,
        .scl_speed_hz = OLED_I2C_HZ,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };

    esp_err_t err = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(OLED_TAG, "panel_io: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_sh1106_config_t sh1106_cfg;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = &sh1106_cfg,
    };

    err = esp_lcd_new_panel_sh1106(s_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(OLED_TAG, "panel: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, false));
    return ESP_OK;
}

static esp_err_t oled_flush_framebuffer(void)
{
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, OLED_W, OLED_H, s_fb);
}

esp_err_t app_oled_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

#if !APP_OLED_ENABLE
    ESP_LOGI(OLED_TAG, "disabled");
    return ESP_ERR_NOT_FOUND;
#endif

    ESP_LOGI(OLED_TAG, "external SH1106 — expect SDA GPIO%d SCL GPIO%d", APP_OLED_PIN_SDA,
             APP_OLED_PIN_SCL);
    printf("\n[OLED] init (external) SDA=GPIO%d SCL=GPIO%d\n", APP_OLED_PIN_SDA, APP_OLED_PIN_SCL);

    vTaskDelay(pdMS_TO_TICKS(400));

    oled_bus_match_t match = {0};
    if (!oled_find_bus(&match)) {
        oled_bus_delete();
        ESP_LOGW(OLED_TAG, "NOT FOUND — no ACK at 0x3C/0x3D (silkscreen 0x78/0x7A)");
        printf("[OLED] NOT FOUND — type 'oled' on serial for line levels + bus scan\n");
        oled_print_line_levels(APP_OLED_PIN_SDA, APP_OLED_PIN_SCL);
        return ESP_ERR_NOT_FOUND;
    }

    s_sda_pin = match.sda;
    s_scl_pin = match.scl;
    ESP_LOGI(OLED_TAG, "found 0x%02X SDA=GPIO%d SCL=GPIO%d", match.addr, s_sda_pin, s_scl_pin);
    printf("[OLED] found 0x%02X on GPIO%d/GPIO%d\n", match.addr, s_sda_pin, s_scl_pin);

    if (s_sda_pin != APP_OLED_PIN_SDA || s_scl_pin != APP_OLED_PIN_SCL) {
        printf("[OLED] Update app_oled.h: PIN_SDA=%d PIN_SCL=%d\n", s_sda_pin, s_scl_pin);
    }

    esp_err_t err = oled_panel_init(match.addr);
    if (err != ESP_OK) {
        return err;
    }

    oled_font_clear(s_fb, OLED_W, OLED_H);
    err = oled_flush_framebuffer();
    if (err != ESP_OK) {
        ESP_LOGE(OLED_TAG, "draw failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(OLED_TAG, "display ready");
    app_oled_show_lines("ESP32-C3", "OLED OK", "", "");
    return ESP_OK;
}

bool app_oled_is_ready(void)
{
    return s_ready;
}

void app_oled_show_lines(const char *line1, const char *line2, const char *line3, const char *line4)
{
    if (!s_ready || s_lock == NULL || s_panel == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return;
    }

    oled_font_clear(s_fb, OLED_W, OLED_H);
    int y = 7;
    if (line1) {
        oled_font_draw_str(s_fb, 0, y, OLED_W, OLED_H, line1);
        y += OLED_LINE_H;
    }
    if (line2) {
        oled_font_draw_str(s_fb, 0, y, OLED_W, OLED_H, line2);
        y += OLED_LINE_H;
    }
    if (line3) {
        oled_font_draw_str(s_fb, 0, y, OLED_W, OLED_H, line3);
        y += OLED_LINE_H;
    }
    if (line4) {
        oled_font_draw_str(s_fb, 0, y, OLED_W, OLED_H, line4);
    }

    esp_err_t err = oled_flush_framebuffer();
    if (err != ESP_OK) {
        ESP_LOGW(OLED_TAG, "flush: %s", esp_err_to_name(err));
    }

    xSemaphoreGive(s_lock);
}
