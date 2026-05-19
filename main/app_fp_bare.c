#include "app_fp_bare.h"

#include <stdio.h>

#include "esp_err.h"
#include "app_fingerprint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_fp_bare_enroll(uint16_t slot_id)
{
    printf("\n\n========== BARE ENROLL (GPIO1) slot %u ==========\n", (unsigned)slot_id);
    printf("3-step enroll (working baseline). Lift finger between steps.\n\n");

    if (!app_fp_is_ready()) {
        printf("[BARE] fingerprint module not ready\n\n");
        return;
    }
    if (slot_id < APP_FP_MIN_USER_ID || slot_id > APP_FP_MAX_USER_ID) {
        printf("[BARE] invalid slot %u\n\n", (unsigned)slot_id);
        return;
    }

    uint8_t count = 0;
    if (app_fp_get_user_count(&count) == ESP_OK) {
        printf("[BARE] templates before: %u\n", (unsigned)count);
    }

    if (app_fp_slot_is_marked(slot_id) || app_fp_slot_occupied(slot_id)) {
        printf("[BARE] clearing slot %u ...\n", (unsigned)slot_id);
        (void)app_fp_clear_slot(slot_id);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    esp_err_t err = app_fp_enroll(slot_id);
    if (err != ESP_OK) {
        printf("[BARE] enroll FAILED at step (ack 0x%02x): %s\n", app_fp_last_ack(),
               app_fp_ack_str(app_fp_last_ack()));
        (void)app_fp_clear_slot(slot_id);
        printf("\n========== BARE ENROLL FAILED ==========\n\n");
        fflush(stdout);
        return;
    }

    if (app_fp_get_user_count(&count) == ESP_OK) {
        printf("\n[BARE] templates after: %u\n", (unsigned)count);
    }
    printf("========== BARE ENROLL OK ==========\n\n");
    fflush(stdout);
}
