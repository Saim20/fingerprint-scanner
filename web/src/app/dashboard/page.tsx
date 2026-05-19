import { createClient } from "@/lib/supabase/server";
import { hasPendingCommand, isDeviceOnline } from "@/lib/device-status";
import type { Device } from "@/lib/types";
import Link from "next/link";

export default async function DashboardPage() {
  const supabase = await createClient();
  const { data: devices } = await supabase
    .from("devices")
    .select("*")
    .order("name");

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <h1 className="text-2xl font-semibold">Devices</h1>
        <Link href="/dashboard/devices/new" className="btn btn-primary no-underline">
          Add device
        </Link>
      </div>
      {!devices?.length ? (
        <p className="text-[var(--muted)]">
          No devices yet. Create one and provision the ESP32 with the API key.
        </p>
      ) : (
        <ul className="grid gap-4 sm:grid-cols-2">
          {(devices as Device[]).map((d) => (
            <li key={d.id}>
              <Link
                href={`/dashboard/devices/${d.id}`}
                className="card block no-underline hover:border-[var(--accent)]"
              >
                <div className="flex items-start justify-between gap-2">
                  <h2 className="font-medium text-[var(--text)]">{d.name}</h2>
                  <span
                    className={`badge ${isDeviceOnline(d) ? "badge-online" : "badge-offline"}`}
                  >
                    {isDeviceOnline(d) ? "Online" : "Offline"}
                  </span>
                </div>
                <p className="text-sm text-[var(--muted)] mt-2">
                  Mode: <strong className="text-[var(--text)]">{d.desired_mode}</strong>
                  {hasPendingCommand(d) && (
                    <span className="badge badge-pending ml-2">Pending</span>
                  )}
                </p>
                <p className="text-xs text-[var(--muted)] mt-1">
                  cmd {d.command_seq} / ack {d.ack_seq}
                  {d.last_seen_at &&
                    ` · seen ${new Date(d.last_seen_at).toLocaleString()}`}
                </p>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
