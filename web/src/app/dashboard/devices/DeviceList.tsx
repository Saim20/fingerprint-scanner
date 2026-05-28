"use client";

import { ConfirmButton } from "@/components/ConfirmButton";
import { deleteDevice } from "@/lib/actions/admin";
import { hasPendingCommand, isDeviceOnline } from "@/lib/device-status";
import type { Device } from "@/lib/types";
import Link from "next/link";
import { useState } from "react";

export function DeviceList({ devices }: { devices: Device[] }) {
  const [error, setError] = useState<string | null>(null);

  if (!devices.length) {
    return (
      <p className="text-[var(--muted)]">
        No devices yet. Create one and provision the ESP32 with the API key.
      </p>
    );
  }

  return (
    <div>
      {error && <p className="text-red-400 text-sm mb-4">{error}</p>}
      <div className="overflow-x-auto">
        <table className="w-full text-sm">
          <thead>
            <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
              <th className="py-3 pr-4">Name</th>
              <th className="py-3 pr-4">Status</th>
              <th className="py-3 pr-4">Mode</th>
              <th className="py-3 pr-4">Last seen</th>
              <th className="py-3 pr-4">Sync</th>
              <th className="py-3 text-right">Actions</th>
            </tr>
          </thead>
          <tbody>
            {devices.map((d) => (
              <tr
                key={d.id}
                className="border-b border-[var(--border)] hover:bg-[rgba(59,130,246,0.06)]"
              >
                <td className="py-3 pr-4">
                  <Link
                    href={`/dashboard/devices/${d.id}`}
                    className="font-medium text-[var(--text)] no-underline hover:text-[var(--accent)]"
                  >
                    {d.name}
                  </Link>
                </td>
                <td className="py-3 pr-4">
                  <span
                    className={`badge ${isDeviceOnline(d) ? "badge-online" : "badge-offline"}`}
                  >
                    {isDeviceOnline(d) ? "Online" : "Offline"}
                  </span>
                </td>
                <td className="py-3 pr-4 capitalize">{d.desired_mode}</td>
                <td className="py-3 pr-4 text-[var(--muted)]">
                  {d.last_seen_at
                    ? new Date(d.last_seen_at).toLocaleString()
                    : "—"}
                </td>
                <td className="py-3 pr-4 text-[var(--muted)]">
                  {d.command_seq} / {d.ack_seq}
                  {hasPendingCommand(d) && (
                    <span className="badge badge-pending ml-1">pending</span>
                  )}
                </td>
                <td className="py-3 text-right">
                  <div className="flex flex-wrap justify-end gap-2">
                    <Link
                      href={`/dashboard/devices/${d.id}`}
                      className="btn btn-ghost text-xs no-underline"
                    >
                      Manage
                    </Link>
                    <ConfirmButton
                      label="Delete"
                      confirmLabel="Delete"
                      variant="ghost"
                      onConfirm={async () => {
                        setError(null);
                        const result = await deleteDevice(d.id);
                        if (result && "error" in result && result.error) {
                          setError(result.error);
                          return;
                        }
                      }}
                    />
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
