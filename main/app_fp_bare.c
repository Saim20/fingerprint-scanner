#include "app_fp_bare.h"

#include <stdio.h>

#include "esp_err.h"
#include "app_fingerprint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_fp_bare_enroll(uint16_t slot_id)
{
    printf("\n\n========== BARE ENROLL (GPIO1) slot %u ==========\n", (unsigned)slot_id);
    printf("Same finger, 3 scans.\n");
    printf("After each capture: lift finger as soon as the sensor finishes ");
    printf("(before step OK).\n\n");

    if (!app_fp_is_ready()) {
        printf("[BARE] fingerprint module not ready\n\n");
        return;
    }
    if (slot_id < APP_FP_MIN_USER_ID || slot_id > APP_FP_MAX_USER_ID) {
        printf("[BARE] invalid slot %u (use %u..%u)\n\n", (unsigned)slot_id,
               (unsigned)APP_FP_MIN_USER_ID, (unsigned)APP_FP_MAX_USER_ID);
        return;
    }

    uint8_t count = 0;
    if (app_fp_get_user_count(&count) == ESP_OK) {
        printf("[BARE] templates in module before: %u\n", (unsigned)count);
    }

    printf("[BARE] clearing slot %u ...\n", (unsigned)slot_id);
    (void)app_fp_clear_slot(slot_id);
    vTaskDelay(pdMS_TO_TICKS(200));

    for (int step = 1; step <= 3; step++) {
        printf("\n[BARE] Step %d/3: place finger flat, hold until capture ends\n", step);
        fflush(stdout);

        esp_err_t err = app_fp_enroll_legacy_step(slot_id, step);
        if (err != ESP_OK) {
            printf("[BARE] step %d FAILED: %s (ack 0x%02x)\n", step,
                   app_fp_ack_str(app_fp_last_ack()), app_fp_last_ack());
            (void)app_fp_clear_slot(slot_id);
            printf("\n========== BARE ENROLL FAILED ==========\n\n");
            fflush(stdout);
            return;
        }

        /*
         * enroll_legacy_step already waited for lift after ENROLL1/2 (before OK).
         * Step OK means capture + lift window are done.
         */
        printf("[BARE] step %d OK — ", step);
        if (step < 3) {
            printf("place same finger again for step %d\n", step + 1);
        } else {
            printf("enroll complete\n");
        }
        fflush(stdout);
    }

    if (app_fp_get_user_count(&count) == ESP_OK) {
        printf("\n[BARE] templates in module after: %u\n", (unsigned)count);
    }
    printf("[BARE] enroll finished — test with finger on sensor (background scan)\n");
    printf("========== BARE ENROLL OK ==========\n\n");
    fflush(stdout);
}
