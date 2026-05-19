# Fingerprint attendance (ESP32-C3 + Supabase)

ESP32-C3 fingerprint scanner with SH1106 OLED, buzzer, local enroll/scan, and optional **Supabase** cloud sync (remote enroll, attendance events, admin web UI).

## Cloud stack (same Supabase project)

1. **Database + Edge Function** — [`supabase/README.md`](supabase/README.md)
   - `supabase link --project-ref YOUR_REF`
   - `supabase db push`
   - `supabase functions deploy device-api`
2. **Web admin** — [`web/README.md`](web/README.md) — `cd web && npm install && npm run dev`
3. **ESP32** — menuconfig: WiFi + `Cloud Supabase URL`, or serial:
   - `cloudurl https://YOUR_REF.supabase.co`
   - `provision <device_api_key>` (from web UI when creating a device)
   - `deviceid <device-uuid>` (same page — enables Supabase Realtime command push)

## Realtime commands (ESP32 ↔ Supabase WebSocket)

The dashboard updates the `devices` row; the ESP32 subscribes over **Supabase Realtime** (same mechanism as the web UI). No MQTT broker required.

1. Deploy edge functions: `device-api` + `device-token` (see [`supabase/README.md`](supabase/README.md))
2. Serial after `provision`: `deviceid <uuid-from-dashboard>`
3. Boot log should show: `app_realtime: channel joined`

HTTP sync still runs as fallback (attendance upload, template drift, person name resolution).

## End-to-end test

1. Sign in to the web app; create a **person** and a **device**; copy the API key.
2. Flash the ESP; set WiFi in menuconfig; run `provision` + `cloudurl` on serial.
3. On the device page, click **Start remote enroll** for that person; complete 3 scans on the sensor.
4. Open **Attendance** — confirm enroll/scan events appear (Realtime).
5. Scan again locally — a `scan` row should appear with the mapped person.

## Hardware

- **MCU:** ESP32-C3 Super Mini (4 MB flash)
- **Display:** external I2C SH1106 128×64 — SDA GPIO5, SCL GPIO6, 3V3/GND
- **Buzzer:** passive piezo on **GPIO10** (+ to GPIO10, − to GND). PWM via LEDC (~2.7 kHz). Use a **passive** buzzer — an active buzzer that beeps on DC will not work.
- **Sensor:** fingerprint module on UART (see `app_fingerprint.c`)

### Buzzer sounds

| Event | Pattern |
|-------|---------|
| Boot ready | Ascending 3-tone chime |
| Fingerprint module missing | Long low triple (error) |
| WiFi connected | Double high pip |
| WiFi lost | Long low tone |
| WiFi failed (retries exhausted) | Error triple |
| Cloud command (enroll/scan/delete/clear) | Short high pip |
| Command cancelled | Short low pip |
| Passive scan toggled | Two-tone sweep (up = on, down = off) |
| Template drift detected | Double medium (warn) |
| GO accepted | Double high pip |
| GO while busy / queue full | Very short blip |
| GO with no command | Double low (deny) |
| Operation starting (enroll/scan/delete/clear) | Medium tone |
| Place finger | Rising two-tone |
| Lift finger | Falling two-tone |
| Enroll step OK | Single tone |
| Enroll complete / clear all | Triple ascending (done) |
| Match / delete OK | Single tone |
| Unknown finger | Single low tone |
| Failure / enroll error | Double low tone |
| Warning (empty slot, no templates) | Double medium |

## Build

```bash
idf.py set-target esp32c3
idf.py menuconfig   # WiFi + Supabase URL/key
idf.py build flash monitor
```

Serial console uses USB Serial/JTAG on the USB-C port (not UART0 on GPIO20/21).
