import { createClient } from "@/lib/supabase/server";
import { toMappingRows } from "@/lib/mapping-rows";
import { sortPeopleByExternalId } from "@/lib/sort-people";
import type { Device, Person } from "@/lib/types";
import Link from "next/link";
import { DeviceDetailClient } from "./DeviceDetailClient";

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

  const { data: peopleRaw } = await supabase.from("people").select("*");
  const people = sortPeopleByExternalId((peopleRaw ?? []) as Person[]);

  const { data: mappings } = await supabase
    .from("fp_mappings")
    .select("fp_slot, person_id, enrolled_at, people(display_name, external_id)")
    .eq("device_id", id)
    .order("fp_slot");

  if (!device) {
    return <p>Device not found.</p>;
  }

  const d = device as Device;
  const mappingRows = toMappingRows(mappings);

  return (
    <div>
      <Link href="/dashboard" className="text-sm text-[var(--muted)]">
        ← Devices
      </Link>
      <h1 className="text-2xl font-semibold mt-2 mb-1">{d.name}</h1>
      <p className="text-sm text-[var(--muted)] mb-2 font-mono">{d.id}</p>

      <DeviceDetailClient
        device={d}
        people={people}
        mappingRows={mappingRows}
        apiKey={apiKey}
      />
    </div>
  );
}
