# Working fingerprint baseline (commit `318495d`)

Reference captured while HEAD is detached at the **working** tree (local enroll/scan, no cloud).
When merging into `main`, **preserve enroll UART behavior from this baseline**; add cloud/OLED on top without breaking it.

---

## What works here

- **3-step enroll**: `ENROLL1` → `ENROLL2` → `ENROLL3` (one command each).
- **Slot IDs**: `1..150` (`APP_FP_MIN_USER_ID` / `APP_FP_MAX_USER_ID`).
- **Enroll payload**: `fp_run_cmd_timeout(cmd, slot_id, 1, 20000, flush)` — third byte `arg_byte3 = 1` (permission).
- **25 retries** per step, **1 s** between retries, **flush RX only on retry** (`flush = attempt > 1`).
- **300 ms delay** between steps **after** releasing UART lock (`vTaskDelay` outside `xSemaphoreGive`).
- **No UART commands between enroll steps** (no `GET_IMAGE`, no `IDENTIFY`, no `SET_TIMEOUT` at init).
- **No post-enroll IDENTIFY** gate for success.
- **No 6-capture** path (`1× ENROLL1 + 4× ENROLL2 + 1× ENROLL3`) in the UI loop.
- **No NVS slot bitmap** in `app_fingerprint.c`.
- Buttons: **GPIO0=enroll, GPIO1=scan, GPIO2=auto, GPIO3=delete** (`app_btn_id_t`, long-press delete = clear all).

---

## `app_fingerprint.c` (617 lines) — critical API

| Function | Behavior |
|----------|----------|
| `app_fp_init` | UART probe only; **no** `0x2E` / `0x28` at boot |
| `app_fp_enroll_step` | Direct `ENROLL1/2/3` by `step`; refresh count on step 3 only |
| `app_fp_enroll` | Loop steps 1..3 calling `app_fp_enroll_step` |
| `app_fp_search` | 1200 ms delay before SEARCH; 12 tries; flush only on try 1 |
| `app_fp_test_sensor` | `GET_IMAGE` — **only** for `touch` command, never mid-enroll |
| `fp_run_cmd` | Enroll/GET_IMAGE/IDENTIFY/SEARCH → 20 s timeout; always `flush_rx=true` on first call via `fp_run_cmd` |

### Frame / ACK

- 8 bytes: `F5 CMD P1 P2 P3 0 CHK F5`
- Enroll success: byte `[4] == 0x00`
- Enroll fail step 2+: often `0x01` (lift/replace finger)
- SEARCH match: byte `[4]` in `1..3`, user id in `[2]`/`[3]`

---

## `station_example_main.c` — enroll flow

```text
run_enroll(slot_id):
  for step 1..3:
    UI: "Step N/3 — press finger..."
    app_fp_enroll_step(slot_id, step)
  on success: sync_template_count, s_last_enrolled_id = slot, bump s_next_enroll_id
  NO app_fp_slot_occupied / NO app_fp_clear_slot before enroll (unless user deletes)
```

- Button enroll: `ACT_ENROLL_NEXT` → `run_enroll(s_next_enroll_id)` → `run_verify_scan(s_last_enrolled_id)`.
- `s_next_enroll_id` = `APP_FP_MIN_USER_ID + count` (sequential slots), not cloud `external_id` 101.

---

## Hardware (unchanged on main)

- UART: **19200**, TX **GPIO4**, RX **GPIO7**, `APP_FP_SWAP_TX_RX=0`
- OLED: SDA **GPIO5**, SCL **GPIO6**
- Buzzer: **GPIO10**

---

## What `main` added (keep, but do not break enroll)

Typical `main` extras (re-apply carefully):

- `app_cloud.c`, `app_realtime.c`, Supabase sync, GO button (GPIO0) for cloud commands
- NVS slot registry, `app_fp_alloc_enroll_slot`, `app_fp_mark_slot_enrolled`
- `FP_CMD_SET_TIMEOUT` (0x2E) = **25** at boot — **good** if module was bricked by tout=0; must not send `GET_IMAGE` between enroll steps
- Optional `app_fp_bare.c` on GPIO1 for debug — must use same 3-step enroll as this baseline

---

## Known failures on `main` (do NOT reintroduce)

1. **`GET_IMAGE` (0x23) between ENROLL1/2/3** — corrupts enroll session; step 1 OK, step 2 instant `0x01`.
2. **6-scan UI with `app_fp_enroll_step` mapping step 2→capture 1, step 3→capture 5** — skips ENROLL2×3; use direct 3-step loop for production enroll.
3. **`app_fp_slot_occupied` / IDENTIFY after enroll** — false "not stored" on this module (`NOUSER` even when count=1).
4. **Cloud slot = person `external_id` (e.g. 101)** — use `app_fp_alloc_enroll_slot()` + report actual slot in `enroll_done`.
5. **Only 5 enroll retries** — too short; use **25**.
6. **Lift-wait before "step 2 press" message** — user must lift as part of finishing step 1; do not prompt "press" then wait 2.5 s with no finger (ENROLL2 fails with no touch).

---

## Merge checklist for `main` (applied on branch `main`)

- [x] `app_fp_enroll_step` = working implementation (3 cmds, 25 tries, 300 ms gap, no GET_IMAGE).
- [x] `run_enroll` / cloud GO path calls **3-step** enroll, not 6-capture default.
- [x] Cloud: `app_fp_clear_slot` only if slot marked/occupied; `app_fp_alloc_enroll_slot`; no IDENTIFY success gate.
- [x] Keep `SET_TIMEOUT 25` at init; no GET_IMAGE between enroll steps; no compare-level init.
- [x] `attendance_task` / cloud: hold `s_busy` during enroll (UART exclusive).
- [x] GPIO0=GO (cloud), GPIO1=bare test (`app_fp_bare.c`).

---

## User technique (still required)

Lift finger between steps; timing at **end of step 1** affects module state. Firmware cannot replace that with `GET_IMAGE` on this module — only **300 ms + user lift** between UART enroll commands.
