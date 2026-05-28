"use client";

import { useDeviceSyncContext } from "@/components/device/DeviceSyncContext";
import { ConfirmButton } from "@/components/ConfirmButton";
import { queueDeviceSlotDelete } from "@/lib/actions/admin";
import { hasPendingCommand } from "@/lib/device-status";
import { useState } from "react";

export function MappingTable() {
  const { deviceId, device, mappings } = useDeviceSyncContext();
  const [error, setError] = useState<string | null>(null);
  const pending = hasPendingCommand(device);

  if (!mappings.length) {
    return (
      <p className="text-[var(--muted)] text-sm">No enrollments on this device yet.</p>
    );
  }

  return (
    <div>
      {error && <p className="text-red-400 text-sm mb-2">{error}</p>}
      <p className="text-xs text-[var(--muted)] mb-3">
        Removing a fingerprint queues delete on the scanner; the row disappears when the device
        confirms (live via Realtime).
      </p>
      <table className="w-full text-sm">
        <thead>
          <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
            <th className="py-2">Device slot</th>
            <th className="py-2">External ID</th>
            <th className="py-2">Person</th>
            <th className="py-2">Enrolled</th>
            <th className="py-2 w-40"></th>
          </tr>
        </thead>
        <tbody>
          {mappings.map((m) => {
            const deleting =
              pending &&
              device.desired_mode === "delete" &&
              device.desired_fp_slot === m.fp_slot;

            return (
              <tr key={m.fp_slot} className="border-b border-[var(--border)]">
                <td className="py-2 font-mono">{m.fp_slot}</td>
                <td className="py-2 font-mono text-[var(--muted)]">
                  {m.externalId ?? "—"}
                </td>
                <td className="py-2">{m.personLabel}</td>
                <td className="py-2 text-[var(--muted)]">
                  {new Date(m.enrolled_at).toLocaleString()}
                </td>
                <td className="py-2 text-right">
                  <ConfirmButton
                    label={deleting ? "Deleting…" : "Delete"}
                    confirmLabel="Delete fingerprint"
                    variant="ghost"
                    disabled={pending}
                    onConfirm={async () => {
                      setError(null);
                      const result = await queueDeviceSlotDelete(deviceId, m.fp_slot);
                      if (result.error) {
                        setError(result.error);
                      }
                    }}
                  />
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
