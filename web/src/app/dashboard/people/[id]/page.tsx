import { createClient } from "@/lib/supabase/server";
import Link from "next/link";

function deviceName(
  devices: { name: string } | { name: string }[] | null,
): string | null {
  if (!devices) return null;
  if (Array.isArray(devices)) return devices[0]?.name ?? null;
  return devices.name;
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
    .select("fp_slot, enrolled_at, devices(name)")
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
      {person.external_id && (
        <p className="text-[var(--muted)] mb-4">External ID: {person.external_id}</p>
      )}

      <h2 className="text-lg font-medium mb-3">Enrolled on devices</h2>
      {!mappings?.length ? (
        <p className="text-[var(--muted)] text-sm">Not enrolled on any device.</p>
      ) : (
        <ul className="space-y-2">
          {mappings.map((m) => (
            <li key={m.fp_slot} className="card text-sm">
              Slot {m.fp_slot} on{" "}
              {deviceName(m.devices) ?? "device"} ·{" "}
              {new Date(m.enrolled_at).toLocaleString()}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
