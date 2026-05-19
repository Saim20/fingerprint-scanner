# Supabase backend (linked project)

Use your **existing** Supabase project — do not create a new one.

## One-time setup

```bash
cd "/home/saim/Documents/Dev/esp/fingerprint scanner"
supabase login
supabase link --project-ref YOUR_PROJECT_REF
```

Edit `supabase/config.toml` and set `project_id` to your ref if needed.

## Database password (required for `db push`)

Remote migrations need your **Postgres database password** (not the publishable/anon API key).

1. Open [Supabase Dashboard](https://supabase.com/dashboard/project/zadlhdxvuvmzrpsbrkbn/settings/database) → **Project Settings** → **Database**.
2. Use **Database password** (or **Reset database password** if you lost it).

Set it for the CLI (pick one):

```bash
# Option A — export in the shell (recommended)
export SUPABASE_DB_PASSWORD='your-database-password'
supabase db push

# Option B — file (gitignored)
cp supabase/.env.example supabase/.env
# Edit supabase/.env and set SUPABASE_DB_PASSWORD=...
set -a && source supabase/.env && set +a
supabase db push
```

If you see *"Connect to your database by setting the env var correctly: SUPABASE_DB_PASSWORD"*, the variable is missing, wrong, or has extra quotes/whitespace.

## Apply schema

```bash
export SUPABASE_DB_PASSWORD='your-database-password'   # if not using supabase/.env
supabase db push
```

## Deploy device API (ESP32)

```bash
supabase functions deploy device-api
```

Function URL: `https://YOUR_PROJECT_REF.supabase.co/functions/v1/device-api`

**ESP32 TLS:** `cloudurl` must be exactly `https://YOUR_REF.supabase.co` (no trailing slash).
If you see `No matching trusted root certificate`, rebuild after updating `sdkconfig.defaults`
(cross-signed cert support for Google Trust / Supabase).

`verify_jwt` is disabled for this function; devices authenticate with `Authorization: Bearer <device_api_key>`.

## Local functions (optional)

```bash
supabase functions serve device-api --env-file supabase/.env.local
```

## Rotate a device key

Regenerate in the web UI (re-hash in DB) or update `api_key_hash` manually. Re-provision the ESP with `provision <new_key>` over serial.
