import { createClient } from "@/lib/supabase/server";
import type { Device, Person } from "@/lib/types";
import Link from "next/link";
import { DeviceCommands } from "./DeviceCommands";
import { ApiKeyBanner } from "./ApiKeyBanner";
import { DeviceLiveStatus } from "./DeviceLiveStatus";
import { DeviceManagePanel } from "./DeviceManagePanel";
import { MappingTable } from "./MappingTable";
import { TemplateSyncPanel } from "./TemplateSyncPanel";

function personFields(
  people:
    | { display_name: string; external_id: string | null }
    | { display_name: string; external_id: string | null }[]
    | null,
): { display_name: string; external_id: string | null } | null {
  if (!people) return null;
  if (Array.isArray(people)) return people[0] ?? null;
  return people;
}

export default async function DeviceDetailPage({
  params,
  searchParams,
}: {
  params: Promise<{ id: string }>;
  searchParams: Promise<{ api_key?: string }>;
}) {
  const { id } = await params;
  const { api_key: apiKey } = await searchParams;
  const supabase = await createClient();

  const { data: device } = await supabase
    .from("devices")
    .select("*")
    .eq("id", id)
    .single();

  const { data: people } = await supabase
    .from("people")
    .select("*")
    .order("display_name");

  const { data: mappings } = await supabase
    .from("fp_mappings")
    .select("fp_slot, person_id, enrolled_at, people(display_name, external_id)")
    .eq("device_id", id)
    .order("fp_slot");

  if (!device) {
    return <p>Device not found.</p>;
  }

  const d = device as Device;
  const mappingRows =
    mappings?.map((m) => {
      const p = personFields(m.people);
      return {
        fp_slot: m.fp_slot,
        person_id: m.person_id,
        enrolled_at: m.enrolled_at,
        personLabel: p?.display_name ?? m.person_id,
        externalId: p?.external_id ?? null,
      };
    }) ?? [];

  const reportedSlots = (d.reported_fp_slots ?? []).map(Number).sort((a, b) => a - b);

  return (
    <div>
      <Link href="/dashboard" className="text-sm text-[var(--muted)]">
        ← Devices
      </Link>
      <h1 className="text-2xl font-semibold mt-2 mb-1">{d.name}</h1>
      <p className="text-sm text-[var(--muted)] mb-2 font-mono">{d.id}</p>

      <DeviceLiveStatus initialDevice={d} people={(people ?? []) as Person[]} />

      {apiKey && <ApiKeyBanner apiKey={apiKey} deviceId={d.id} />}

      <DeviceCommands
        deviceId={d.id}
        people={(people ?? []) as Person[]}
        reportedSlots={reportedSlots}
        backgroundScan={d.background_scan ?? false}
      />

      <TemplateSyncPanel
        deviceId={d.id}
        reportedSlots={reportedSlots}
        reportedCount={d.reported_fp_count ?? null}
        lastSyncAt={d.last_template_sync_at ?? null}
        mappings={mappingRows}
        people={(people ?? []) as Person[]}
      />

      <section className="mt-8">
        <h2 className="text-lg font-medium mb-3">Fingerprint mappings</h2>
        <MappingTable deviceId={d.id} mappings={mappingRows} />
      </section>

      <DeviceManagePanel deviceId={d.id} deviceName={d.name} />
    </div>
  );
}
