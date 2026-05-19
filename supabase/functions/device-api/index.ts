import { createClient } from "https://esm.sh/@supabase/supabase-js@2.49.1";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type",
};

async function sha256Hex(input: string): Promise<string> {
  const data = new TextEncoder().encode(input);
  const hash = await crypto.subtle.digest("SHA-256", data);
  return Array.from(new Uint8Array(hash))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders, "Content-Type": "application/json" },
  });
}

function getBearer(req: Request): string | null {
  const auth = req.headers.get("Authorization");
  if (!auth?.startsWith("Bearer ")) return null;
  return auth.slice(7).trim();
}

type DeviceRow = {
  id: string;
  name: string;
  api_key_hash: string;
  desired_mode: string;
  desired_person_id: string | null;
  desired_fp_slot: number;
  command_seq: number;
  ack_seq: number;
};

async function findDeviceByKey(
  supabase: ReturnType<typeof createClient>,
  apiKey: string,
): Promise<DeviceRow | null> {
  const hash = await sha256Hex(apiKey);
  const { data, error } = await supabase
    .from("devices")
    .select(
      "id, name, api_key_hash, desired_mode, desired_person_id, desired_fp_slot, command_seq, ack_seq",
    )
    .eq("api_key_hash", hash)
    .maybeSingle();
  if (error || !data) return null;
  return data as DeviceRow;
}

async function handleSync(
  supabase: ReturnType<typeof createClient>,
  device: DeviceRow,
): Promise<Response> {
  await supabase
    .from("devices")
    .update({ last_seen_at: new Date().toISOString() })
    .eq("id", device.id);

  let person_display_name: string | null = null;
  if (device.desired_person_id) {
    const { data: person } = await supabase
      .from("people")
      .select("display_name")
      .eq("id", device.desired_person_id)
      .maybeSingle();
    person_display_name = person?.display_name ?? null;
  }

  return jsonResponse({
    command_seq: device.command_seq,
    desired_mode: device.desired_mode,
    desired_person_id: device.desired_person_id,
    desired_fp_slot: device.desired_fp_slot,
    person_display_name,
  });
}

async function handleEvent(
  supabase: ReturnType<typeof createClient>,
  device: DeviceRow,
  body: Record<string, unknown>,
): Promise<Response> {
  const type = body.type as string;

  if (type === "ack") {
    const command_seq = Number(body.command_seq);
    if (!Number.isFinite(command_seq)) {
      return jsonResponse({ error: "invalid command_seq" }, 400);
    }
    await supabase
      .from("devices")
      .update({ ack_seq: command_seq })
      .eq("id", device.id);
    return jsonResponse({ ok: true });
  }

  if (type === "scan") {
    const fp_slot = Number(body.fp_slot);
    if (!Number.isFinite(fp_slot) || fp_slot < 1 || fp_slot > 150) {
      return jsonResponse({ error: "invalid fp_slot" }, 400);
    }

    const { data: mapping } = await supabase
      .from("fp_mappings")
      .select("person_id")
      .eq("device_id", device.id)
      .eq("fp_slot", fp_slot)
      .maybeSingle();

    await supabase.from("attendance_events").insert({
      device_id: device.id,
      fp_slot,
      person_id: mapping?.person_id ?? null,
      event_type: "scan",
    });

    return jsonResponse({
      ok: true,
      person_id: mapping?.person_id ?? null,
    });
  }

  if (type === "enroll_done") {
    const fp_slot = Number(body.fp_slot);
    const person_id = body.person_id as string;
    if (
      !Number.isFinite(fp_slot) ||
      fp_slot < 1 ||
      fp_slot > 150 ||
      !person_id
    ) {
      return jsonResponse({ error: "invalid enroll_done payload" }, 400);
    }

    await supabase.from("fp_mappings").upsert(
      {
        device_id: device.id,
        fp_slot,
        person_id,
        enrolled_at: new Date().toISOString(),
      },
      { onConflict: "device_id,fp_slot" },
    );

    await supabase.from("attendance_events").insert({
      device_id: device.id,
      fp_slot,
      person_id,
      event_type: "enroll",
    });

    await supabase
      .from("devices")
      .update({
        desired_mode: "idle",
        desired_person_id: null,
        desired_fp_slot: 0,
        ack_seq: device.command_seq,
      })
      .eq("id", device.id);

    return jsonResponse({ ok: true });
  }

  return jsonResponse({ error: "unknown event type" }, 400);
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse({ error: "method not allowed" }, 405);
  }

  const apiKey = getBearer(req);
  if (!apiKey) {
    return jsonResponse({ error: "missing bearer token" }, 401);
  }

  const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
  const serviceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
  const supabase = createClient(supabaseUrl, serviceKey);

  const device = await findDeviceByKey(supabase, apiKey);
  if (!device) {
    return jsonResponse({ error: "invalid device key" }, 401);
  }

  const url = new URL(req.url);
  const action = url.searchParams.get("action") ?? "sync";

  let body: Record<string, unknown> = {};
  const text = await req.text();
  if (text) {
    try {
      body = JSON.parse(text);
    } catch {
      return jsonResponse({ error: "invalid json" }, 400);
    }
  }

  if (action === "sync") {
    return handleSync(supabase, device);
  }

  if (action === "event") {
    return handleEvent(supabase, device, body);
  }

  return jsonResponse({ error: "unknown action; use ?action=sync or ?action=event" }, 400);
});
