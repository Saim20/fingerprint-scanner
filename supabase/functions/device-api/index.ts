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
  background_scan?: boolean;
};

type MappingRow = {
  fp_slot: number;
  person_id: string;
  people: {
    display_name: string;
    external_id: string | null;
  } | null;
};

type MappingOut = {
  fp_slot: number;
  person_id: string;
  display_name: string;
  external_id: string | null;
};

function parseFpSlots(body: Record<string, unknown>): number[] {
  const raw = body.fp_slots;
  if (!Array.isArray(raw)) return [];
  const slots: number[] = [];
  for (const v of raw) {
    const n = Number(v);
    if (Number.isFinite(n) && n >= 1 && n <= 150) {
      slots.push(n);
    }
  }
  return [...new Set(slots)].sort((a, b) => a - b);
}

function slotsHash(slots: number[]): string {
  return slots.join(",");
}

async function findDeviceByKey(
  supabase: ReturnType<typeof createClient>,
  apiKey: string,
): Promise<DeviceRow | null> {
  const hash = await sha256Hex(apiKey);
  const { data, error } = await supabase
    .from("devices")
    .select(
      "id, name, api_key_hash, desired_mode, desired_person_id, desired_fp_slot, command_seq, ack_seq, background_scan",
    )
    .eq("api_key_hash", hash)
    .maybeSingle();
  if (error || !data) return null;
  return data as DeviceRow;
}

async function loadDeviceMappings(
  supabase: ReturnType<typeof createClient>,
  deviceId: string,
): Promise<MappingOut[]> {
  const { data } = await supabase
    .from("fp_mappings")
    .select("fp_slot, person_id, people(display_name, external_id)")
    .eq("device_id", deviceId)
    .order("fp_slot");

  if (!data) return [];

  return (data as MappingRow[]).map((m) => ({
    fp_slot: m.fp_slot,
    person_id: m.person_id,
    display_name: m.people?.display_name ?? "",
    external_id: m.people?.external_id ?? null,
  }));
}

function reconcileSlots(
  deviceSlots: number[],
  mappings: MappingOut[],
): { unmapped_slots: number[]; stale_slots: number[] } {
  const deviceSet = new Set(deviceSlots);
  const mappedSlots = new Set(mappings.map((m) => m.fp_slot));

  const unmapped_slots = deviceSlots.filter((s) => !mappedSlots.has(s));
  const stale_slots = mappings
    .map((m) => m.fp_slot)
    .filter((s) => !deviceSet.has(s));

  return { unmapped_slots, stale_slots };
}

/** If person.external_id is numeric 1–150, use as preferred device slot. */
function preferredSlotFromPerson(
  externalId: string | null | undefined,
): number {
  if (!externalId) return 0;
  const n = Number(externalId.trim());
  if (Number.isInteger(n) && n >= 1 && n <= 150) {
    return n;
  }
  return 0;
}

async function handleSync(
  supabase: ReturnType<typeof createClient>,
  device: DeviceRow,
  body: Record<string, unknown>,
): Promise<Response> {
  const fp_slots = parseFpSlots(body);
  const fp_count = Number(body.fp_count);
  const slots_hash =
    typeof body.slots_hash === "string" && body.slots_hash.length > 0
      ? body.slots_hash
      : slotsHash(fp_slots);

  const deviceUpdate: Record<string, unknown> = {
    last_seen_at: new Date().toISOString(),
    last_template_sync_at: new Date().toISOString(),
    reported_slots_hash: slots_hash,
    reported_fp_slots: fp_slots,
    reported_fp_count: Number.isFinite(fp_count) && fp_count >= 0 && fp_count <= 150
      ? fp_count
      : fp_slots.length,
  };

  await supabase.from("devices").update(deviceUpdate).eq("id", device.id);

  const mappings = await loadDeviceMappings(supabase, device.id);
  const { unmapped_slots, stale_slots } =
    fp_slots.length > 0
      ? reconcileSlots(fp_slots, mappings)
      : { unmapped_slots: [] as number[], stale_slots: [] as number[] };

  let person_display_name: string | null = null;
  let person_external_id: string | null = null;
  let effective_fp_slot = device.desired_fp_slot;

  if (device.desired_person_id) {
    const { data: person } = await supabase
      .from("people")
      .select("display_name, external_id")
      .eq("id", device.desired_person_id)
      .maybeSingle();

    person_display_name = person?.display_name ?? null;
    person_external_id = person?.external_id ?? null;

    if (
      device.desired_mode === "add" &&
      device.desired_fp_slot === 0 &&
      person_external_id
    ) {
      const pref = preferredSlotFromPerson(person_external_id);
      if (pref > 0) {
        effective_fp_slot = pref;
      }
    }
  }

  return jsonResponse({
    command_seq: device.command_seq,
    ack_seq: device.ack_seq,
    desired_mode: device.desired_mode,
    desired_person_id: device.desired_person_id,
    desired_fp_slot: effective_fp_slot,
    person_display_name,
    person_external_id,
    mappings,
    unmapped_slots,
    stale_slots,
    background_scan: device.background_scan ?? false,
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
      .select("person_id, people(display_name, external_id)")
      .eq("device_id", device.id)
      .eq("fp_slot", fp_slot)
      .maybeSingle();

    const person = mapping?.people as
      | { display_name: string; external_id: string | null }
      | null;

    await supabase.from("attendance_events").insert({
      device_id: device.id,
      fp_slot,
      person_id: mapping?.person_id ?? null,
      event_type: "scan",
    });

    return jsonResponse({
      ok: true,
      person_id: mapping?.person_id ?? null,
      display_name: person?.display_name ?? null,
      external_id: person?.external_id ?? null,
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

    const { data: devRow } = await supabase
      .from("devices")
      .select("reported_fp_slots")
      .eq("id", device.id)
      .maybeSingle();

    const prevSlots = (devRow?.reported_fp_slots as number[] | null) ?? [];
    const nextSlots = prevSlots.includes(fp_slot)
      ? prevSlots
      : [...prevSlots, fp_slot].sort((a, b) => a - b);

    await supabase
      .from("devices")
      .update({
        desired_mode: "idle",
        desired_person_id: null,
        desired_fp_slot: 0,
        ack_seq: device.command_seq,
        reported_fp_slots: nextSlots,
        reported_fp_count: nextSlots.length,
        last_template_sync_at: new Date().toISOString(),
      })
      .eq("id", device.id);

    return jsonResponse({ ok: true });
  }

  if (type === "slot_cleared") {
    const fp_slot = Number(body.fp_slot);
    if (!Number.isFinite(fp_slot) || fp_slot < 1 || fp_slot > 150) {
      return jsonResponse({ error: "invalid fp_slot" }, 400);
    }

    await supabase
      .from("fp_mappings")
      .delete()
      .eq("device_id", device.id)
      .eq("fp_slot", fp_slot);

    const { data: devRow } = await supabase
      .from("devices")
      .select("reported_fp_slots")
      .eq("id", device.id)
      .maybeSingle();

    const prevSlots = (devRow?.reported_fp_slots as number[] | null) ?? [];
    const nextSlots = prevSlots.filter((s) => s !== fp_slot).sort((a, b) => a - b);

    const devicePatch: Record<string, unknown> = {
      reported_fp_slots: nextSlots,
      reported_fp_count: nextSlots.length,
      last_template_sync_at: new Date().toISOString(),
    };

    if (
      device.desired_mode === "delete" &&
      device.desired_fp_slot === fp_slot &&
      device.command_seq > device.ack_seq
    ) {
      devicePatch.desired_mode = "idle";
      devicePatch.desired_fp_slot = 0;
      devicePatch.ack_seq = device.command_seq;
    }

    await supabase.from("devices").update(devicePatch).eq("id", device.id);

    return jsonResponse({ ok: true });
  }

  if (type === "all_cleared") {
    await supabase.from("fp_mappings").delete().eq("device_id", device.id);

    const devicePatch: Record<string, unknown> = {
      reported_fp_slots: [],
      reported_fp_count: 0,
      reported_slots_hash: "",
      last_template_sync_at: new Date().toISOString(),
    };

    if (device.desired_mode === "clear" && device.command_seq > device.ack_seq) {
      devicePatch.desired_mode = "idle";
      devicePatch.desired_fp_slot = 0;
      devicePatch.ack_seq = device.command_seq;
    }

    await supabase.from("devices").update(devicePatch).eq("id", device.id);

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
    return handleSync(supabase, device, body);
  }

  if (action === "event") {
    return handleEvent(supabase, device, body);
  }

  return jsonResponse(
    { error: "unknown action; use ?action=sync or ?action=event" },
    400,
  );
});
