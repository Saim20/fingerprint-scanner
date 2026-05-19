import { createClient } from "@/lib/supabase/server";
import { hasPendingCommand, isDeviceOnline } from "@/lib/device-status";
import type { Device, Person } from "@/lib/types";
import Link from "next/link";
import { DeviceCommands } from "./DeviceCommands";
import { ApiKeyBanner } from "./ApiKeyBanner";
import { DeviceManagePanel } from "./DeviceManagePanel";
import { MappingTable } from "./MappingTable";

function personName(
  people: { display_name: string } | { display_name: string }[] | null,
): string | null {
  if (!people) return null;
  if (Array.isArray(people)) return people[0]?.display_name ?? null;
  return people.display_name;
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
    .select("fp_slot, person_id, enrolled_at, people(display_name)")
    .eq("device_id", id)
    .order("fp_slot");

  if (!device) {
    return <p>Device not found.</p>;
  }

  const d = device as Device;
  const mappingRows =
    mappings?.map((m) => ({
      fp_slot: m.fp_slot,
      person_id: m.person_id,
      enrolled_at: m.enrolled_at,
      personLabel: personName(m.people) ?? m.person_id,
    })) ?? [];

  return (
    <div>
      <Link href="/dashboard" className="text-sm text-[var(--muted)]">
        ← Devices
      </Link>
      <h1 className="text-2xl font-semibold mt-2 mb-1">{d.name}</h1>
      <p className="text-sm text-[var(--muted)] mb-2 font-mono">{d.id}</p>
      <p className="text-sm text-[var(--muted)] mb-6">
        <span className={`badge ${isDeviceOnline(d) ? "badge-online" : "badge-offline"}`}>
          {isDeviceOnline(d) ? "Online" : "Offline"}
        </span>
        {hasPendingCommand(d) && (
          <span className="badge badge-pending ml-2">Command pending</span>
        )}
        {" · "}
        Mode: {d.desired_mode} · cmd {d.command_seq} / ack {d.ack_seq}
        {d.last_seen_at && (
          <> · Last seen {new Date(d.last_seen_at).toLocaleString()}</>
        )}
      </p>

      {apiKey && <ApiKeyBanner apiKey={apiKey} />}

      <DeviceCommands deviceId={d.id} people={(people ?? []) as Person[]} />

      <section className="mt-8">
        <h2 className="text-lg font-medium mb-3">Fingerprint mappings</h2>
        <MappingTable deviceId={d.id} mappings={mappingRows} />
      </section>

      <DeviceManagePanel deviceId={d.id} deviceName={d.name} />
    </div>
  );
}
