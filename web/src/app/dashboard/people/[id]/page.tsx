import { createClient } from "@/lib/supabase/server";
import Link from "next/link";
import { PersonManagePanel } from "../PersonManagePanel";

function deviceName(
  devices: { name: string; id?: string } | { name: string; id?: string }[] | null,
): string | null {
  if (!devices) return null;
  if (Array.isArray(devices)) return devices[0]?.name ?? null;
  return devices.name;
}

function deviceId(
  devices: { name: string; id?: string } | { name: string; id?: string }[] | null,
): string | null {
  if (!devices) return null;
  if (Array.isArray(devices)) return devices[0]?.id ?? null;
  return devices.id ?? null;
}

export default async function PersonDetailPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const { id } = await params;
  const supabase = await createClient();

  const { data: person } = await supabase
    .from("people")
    .select("*")
    .eq("id", id)
    .single();

  const { data: mappings } = await supabase
    .from("fp_mappings")
    .select("fp_slot, enrolled_at, device_id, devices(id, name)")
    .eq("person_id", id);

  if (!person) {
    return <p>Person not found.</p>;
  }

  return (
    <div>
      <Link href="/dashboard/people" className="text-sm text-[var(--muted)]">
        ← People
      </Link>
      <h1 className="text-2xl font-semibold mt-2 mb-6">{person.display_name}</h1>

      <PersonManagePanel
        personId={person.id}
        displayName={person.display_name}
        externalId={person.external_id}
      />

      <h2 className="text-lg font-medium mb-3">Enrolled on devices</h2>
      {!mappings?.length ? (
        <p className="text-[var(--muted)] text-sm">Not enrolled on any device.</p>
      ) : (
        <ul className="space-y-2">
          {mappings.map((m) => {
            const devId = deviceId(m.devices) ?? m.device_id;
            const name = deviceName(m.devices) ?? "device";
            return (
              <li key={`${devId}-${m.fp_slot}`} className="card text-sm flex justify-between items-center gap-4">
                <span>
                  Slot {m.fp_slot} on{" "}
                  <Link href={`/dashboard/devices/${devId}`} className="font-medium">
                    {name}
                  </Link>
                  {" · "}
                  {new Date(m.enrolled_at).toLocaleString()}
                </span>
                <Link
                  href={`/dashboard/devices/${devId}`}
                  className="btn btn-ghost text-xs no-underline shrink-0"
                >
                  Manage device
                </Link>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}
