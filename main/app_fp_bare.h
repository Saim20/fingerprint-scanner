#pragma once

#include <stdint.h>

/** Fixed slot for GPIO1 bare-minimum enroll test. */
#define APP_FP_BARE_SLOT 1

/**
 * Minimal enroll: clear slot, then ENROLL1 → ENROLL2 → ENROLL3 (one scan each).
 * printf only — no cloud, no OLED, no NVS slot registry.
 */
void app_fp_bare_enroll(uint16_t slot_id);
