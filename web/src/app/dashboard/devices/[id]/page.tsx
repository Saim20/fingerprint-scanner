import { createClient } from "@/lib/supabase/server";
import { hasPendingCommand, isDeviceOnline } from "@/lib/device-status";
import type { Device, Person } from "@/lib/types";
import Link from "next/link";
import { DeviceCommands } from "./DeviceCommands";
import { ApiKeyBanner } from "./ApiKeyBanner";

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

  return (
    <div>
      <Link href="/dashboard" className="text-sm text-[var(--muted)]">
        ← Devices
      </Link>
      <h1 className="text-2xl font-semibold mt-2 mb-1">{d.name}</h1>
      <p className="text-sm text-[var(--muted)] mb-6">
        <span className={`badge ${isDeviceOnline(d) ? "badge-online" : "badge-offline"}`}>
          {isDeviceOnline(d) ? "Online" : "Offline"}
        </span>
        {hasPendingCommand(d) && (
          <span className="badge badge-pending ml-2">Command pending</span>
        )}
        {" · "}
        Mode: {d.desired_mode} · cmd {d.command_seq} / ack {d.ack_seq}
      </p>

      {apiKey && <ApiKeyBanner apiKey={apiKey} />}

      <DeviceCommands deviceId={d.id} people={(people ?? []) as Person[]} />

      <section className="mt-8">
        <h2 className="text-lg font-medium mb-3">Fingerprint mappings</h2>
        {!mappings?.length ? (
          <p className="text-[var(--muted)] text-sm">No enrollments on this device yet.</p>
        ) : (
          <table className="w-full text-sm">
            <thead>
              <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
                <th className="py-2">Slot</th>
                <th className="py-2">Person</th>
                <th className="py-2">Enrolled</th>
              </tr>
            </thead>
            <tbody>
              {mappings.map((m) => (
                <tr key={m.fp_slot} className="border-b border-[var(--border)]">
                  <td className="py-2">{m.fp_slot}</td>
                  <td className="py-2">
                    {personName(m.people) ?? m.person_id}
                  </td>
                  <td className="py-2 text-[var(--muted)]">
                    {new Date(m.enrolled_at).toLocaleString()}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>
    </div>
  );
}
