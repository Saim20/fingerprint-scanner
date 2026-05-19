#include "app_fp_bare.h"

#include <stdio.h>

#include "esp_err.h"
#include "app_fingerprint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_fp_bare_enroll(uint16_t slot_id)
{
    printf("\n\n========== BARE ENROLL (GPIO1) slot %u ==========\n", (unsigned)slot_id);
    printf("3 steps: ENROLL1 -> ENROLL2 -> ENROLL3 (same as working firmware).\n");
    printf("Lift finger between steps; do not use GET_IMAGE mid-sequence.\n\n");

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

    printf("[BARE] clearing slot %u ...\n", (unsigned)slot_id);
    (void)app_fp_clear_slot(slot_id);
    vTaskDelay(pdMS_TO_TICKS(200));

    for (int step = 1; step <= APP_FP_ENROLL_STEPS; step++) {
        printf("\n[BARE] Step %d/%d — press finger, hold 3-5 sec, lift when done\n", step,
               APP_FP_ENROLL_STEPS);
        fflush(stdout);

        esp_err_t err = app_fp_enroll_step(slot_id, step);
        if (err != ESP_OK) {
            printf("[BARE] step %d FAILED: %s (ack 0x%02x)\n", step,
                   app_fp_ack_str(app_fp_last_ack()), app_fp_last_ack());
            (void)app_fp_clear_slot(slot_id);
            printf("\n========== BARE ENROLL FAILED ==========\n\n");
            fflush(stdout);
            return;
        }
        printf("[BARE] step %d OK\n", step);
        fflush(stdout);
    }

    (void)app_fp_mark_slot_enrolled(slot_id);

    if (app_fp_get_user_count(&count) == ESP_OK) {
        printf("\n[BARE] templates after: %u\n", (unsigned)count);
    }
    printf("========== BARE ENROLL OK ==========\n\n");
    fflush(stdout);
}
