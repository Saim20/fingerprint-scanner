# Fingerprint attendance admin

Next.js dashboard for devices, people, remote enroll commands, and live attendance.

## Setup

```bash
cd web
cp .env.example .env.local
# Edit .env.local with your linked Supabase project URL and publishable key
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000). Sign in with a user created in Supabase Auth (Dashboard → Authentication → Users).

## Before first use

1. Apply DB migration: `supabase db push` (from repo root).
2. Deploy Edge Function: `supabase functions deploy device-api`.
3. Create a device in the UI and copy the API key to the ESP (`provision <key>`).
