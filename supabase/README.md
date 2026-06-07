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

### `permission denied to alter role` / `cli_login_postgres` (400)

Some hosted projects reject the CLI’s automatic login-role setup. You do **not** need `db push` for CSV import (the web app uses select/update/insert). For schema changes, use either:

**A — SQL Editor (simplest)**  
Dashboard → **SQL** → New query → paste the migration file → Run.

Required for live enrollment roster updates on the People page:

```sql
alter publication supabase_realtime add table public.fp_mappings;
```

(Also in `migrations/20250520110000_realtime_fp_mappings.sql`.)

**B — Direct URL (bypass login role)**  
Dashboard → **Database** → **Connection string** → URI (Session mode). Then:

```bash
export SUPABASE_DB_PASSWORD='your-database-password'
supabase db push --db-url "postgresql://postgres.YOUR_REF:${SUPABASE_DB_PASSWORD}@aws-0-REGION.pooler.supabase.com:5432/postgres"
```

Replace `YOUR_REF`, `REGION`, and password. Update the CLI: `supabase update`.

## Apply schema

```bash
export SUPABASE_DB_PASSWORD='your-database-password'   # if not using supabase/.env
supabase db push
```

After the initial schema, apply template-sync migration (`20250519100000_template_sync.sql`) with the same `supabase db push` command.

## Deploy edge functions (ESP32)

**Before deploy — JWT secret for Realtime (required for `device-token`):**

Dashboard → **Project Settings** → **API** → **JWT Settings** → copy **JWT Secret**, then:

```bash
supabase secrets set JWT_SECRET='paste-your-jwt-secret-here'
```

Without this, the ESP32 logs `HTTP 500 body={"error":"JWT secret not configured"}` and Realtime never connects (HTTP sync still works).

```bash
supabase functions deploy device-api
supabase functions deploy device-token
```

- **device-api** — HTTP sync + events (`?action=sync|event`)
- **device-token** — mints short-lived JWT for Supabase Realtime WebSocket on the device

Function URLs: `https://YOUR_PROJECT_REF.supabase.co/functions/v1/device-api` and `.../device-token`

**ESP32 TLS:** `cloudurl` must be exactly `https://YOUR_REF.supabase.co` (no trailing slash).
If you see `No matching trusted root certificate`, rebuild after updating `sdkconfig.defaults`
(cross-signed cert support for Google Trust / Supabase).

`verify_jwt` is disabled for these functions; devices authenticate with `Authorization: Bearer <device_api_key>`.

## Local functions (optional)

```bash
supabase functions serve device-api --env-file supabase/.env.local
supabase functions serve device-token --env-file supabase/.env.local
```

## Rotate a device key

Regenerate in the web UI (re-hash in DB) or update `api_key_hash` manually. Re-provision the ESP with `provision <new_key>` over serial.
