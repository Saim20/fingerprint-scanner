# Pin layout reference

ESP32-C3 Super Mini (4 MB flash) — fingerprint attendance firmware (buzzer feedback only).

Serial console and flashing use **USB Serial/JTAG** on the USB-C port. Do **not** rely on UART0 (GPIO20 TX / GPIO21 RX) for `idf.py monitor`.

---

## Quick reference

| GPIO | Function | Peripheral | Notes |
|------|----------|------------|-------|
| **0** | GO button | Input (pull-up) | Run pending cloud command from dashboard |
| **1–3** | Spare | — | GPIO2 is a strapping pin — do not hold low at reset |
| **4** | Fingerprint TX | UART1 TX | Cross-wire to module **RX** |
| **5–6** | — | — | Unused |
| **7** | Fingerprint RX | UART1 RX | Cross-wire to module **TX** |
| **8–9** | — | — | Unused |
| **10** | Buzzer (+) | LEDC PWM ~2.7 kHz | Passive piezo only; − to GND |
| **11–19** | — | — | Unused |
| **20** | UART0 TX | — | Reserved (USB serial path used instead) |
| **21** | UART0 RX | — | Reserved (USB serial path used instead) |

Source defines: `main/app_buttons.h`, `main/app_fingerprint.h`, `main/app_buzzer.c`.

---

## Wiring

### Fingerprint module (FPC1020A / UART)

| ESP32-C3 | Module | Notes |
|----------|--------|-------|
| GPIO4 (TX) | RX | ESP transmits → module receives |
| GPIO7 (RX) | TX | ESP receives ← module transmits |
| 3V3 | VCC | |
| GND | GND | |

- **Baud:** 19200 (`APP_FP_BAUD`)
- **UART:** UART1
- If the module does not respond at boot, **swap the TX/RX dupont wires** (firmware auto-probes both orientations).
- Optional compile-time swap: set `APP_FP_SWAP_TX_RX` to `1` in `main/app_fingerprint.h`.

### Buzzer (passive piezo)

| ESP32-C3 | Buzzer |
|----------|--------|
| GPIO10 | + (signal) |
| GND | − |

Use a **passive** buzzer. An active buzzer that beeps on DC will not work with LEDC PWM.

### GO button (B3F-4055 or similar)

One leg to GPIO0, other leg to **GND**. Internal pull-up: released = **1**, pressed = **0**.

```
3V3 (internal pull-up)
  │
GPIO0 ──[ SW ]── GND     GO (cloud command)
```

---

## Pin usage map

```
ESP32-C3 Super Mini (firmware assignment)

     ┌─────────────────────────────────────┐
     │  USB-C  ← serial / flash / monitor  │
     └─────────────────────────────────────┘

  GPIO0  [GO btn]
  GPIO1–3  (spare)
  GPIO4  ──TX──►  FP module RX
  GPIO7  ◄──RX──  FP module TX
  GPIO10 ──(+ )──  Buzzer
  GPIO20/21  UART0 — not used (USB-JTAG console)
```

---

## Do not use for GPIO peripherals

| GPIO | Reason |
|------|--------|
| **4, 7** | Fingerprint UART1 |
| **10** | Buzzer PWM |
| **20, 21** | UART0 / USB serial routing |
| **2** | Strapping — avoid external loads that pull low during reset |

---

## Power

| Rail | Connect |
|------|---------|
| **3V3** | Fingerprint module VCC |
| **GND** | Common ground for all peripherals |

Do not feed 5 V logic into ESP32-C3 GPIO without level shifting.

---

## Changing pins

Edit the header defines and rebuild:

| Peripheral | File | Defines |
|------------|------|---------|
| Fingerprint UART | `main/app_fingerprint.h` | `APP_FP_PIN_TX`, `APP_FP_PIN_RX`, `APP_FP_BAUD`, `APP_FP_SWAP_TX_RX` |
| GO button | `main/app_buttons.h` | `APP_BTN_PIN_GO` |
| Buzzer | `main/app_buzzer.c` | `BUZZ_GPIO` |

After wiring changes, run `idf.py build flash monitor` and check boot logs for `FPC1020A OK`.
