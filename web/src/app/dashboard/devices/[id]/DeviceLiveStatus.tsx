"use client";

import { useDeviceSyncContext } from "@/components/device/DeviceSyncContext";
import {
  commandNeedsGo,
  hasPendingCommand,
  isDeviceOnline,
} from "@/lib/device-status";
import { externalIdKey } from "@/lib/sort-people";
import type { DeviceMode } from "@/lib/types";

function pendingMessage(
  device: ReturnType<typeof useDeviceSyncContext>["device"],
  people: ReturnType<typeof useDeviceSyncContext>["people"],
): string | null {
  if (!hasPendingCommand(device)) return null;

  const mode = device.desired_mode as DeviceMode;
  const slot = device.desired_fp_slot;
  const person = people.find((p) => p.id === device.desired_person_id);
  const personLabel = person
    ? (() => {
        const id = externalIdKey(person.external_id);
        return id ? `${person.display_name} (${id})` : person.display_name;
      })()
    : null;

  switch (mode) {
    case "add":
      return personLabel
        ? `Enroll ${personLabel} — press GO on the device (slot assigned automatically)`
        : "Enroll queued — press GO on the device";
    case "scan":
      return "Test scan queued — press GO on the device";
    case "delete":
      return slot > 0
        ? `Deleting slot ${slot} on device…`
        : "Delete queued — running on device…";
    case "clear":
      return "Clearing all fingerprints on device…";
    case "idle":
      return "Cancelling command on device…";
    default:
      return "Command running on device…";
  }
}

function pendingSubtext(mode: DeviceMode | string): string {
  if (commandNeedsGo(mode)) {
    return "Press the GO button (GPIO0) on the scanner to start enroll or test scan.";
  }
  return "Runs automatically on the device when online — no GO button needed.";
}

export function DeviceLiveStatus() {
  const { device, people, reportedSlots } = useDeviceSyncContext();

  const online = isDeviceOnline(device);
  const pending = hasPendingCommand(device);
  const pendingText = pendingMessage(device, people);
  const mode = device.desired_mode as DeviceMode;

  return (
    <div className="space-y-3 mb-6">
      <p className="text-sm text-[var(--muted)]">
        <span className={`badge ${online ? "badge-online" : "badge-offline"}`}>
          {online ? "Online" : "Offline"}
        </span>
        {pending && <span className="badge badge-pending ml-2">Command pending</span>}
        {(device.background_scan ?? false) ? (
          <span className="badge badge-online ml-2">Passive scan</span>
        ) : (
          <span className="badge badge-offline ml-2">Command mode</span>
        )}
        {" · "}
        Mode: {device.desired_mode} · cmd {device.command_seq} / ack {device.ack_seq}
        {device.last_seen_at && (
          <> · Last seen {new Date(device.last_seen_at).toLocaleString()}</>
        )}
      </p>

      {pendingText && (
        <div className="rounded-lg border border-amber-500/40 bg-amber-500/10 px-4 py-3 text-sm">
          <p className="font-medium text-amber-300">{pendingText}</p>
          <p className="text-[var(--muted)] mt-1">{pendingSubtext(mode)}</p>
        </div>
      )}

      {(reportedSlots.length > 0 || device.reported_fp_count != null) && (
        <p className="text-sm">
          <span className="text-[var(--muted)]">On device:</span>{" "}
          {reportedSlots.length
            ? reportedSlots.join(", ")
            : `${device.reported_fp_count} template(s)`}
          {device.last_template_sync_at && (
            <span className="text-[var(--muted)]">
              {" "}
              · synced {new Date(device.last_template_sync_at).toLocaleString()}
            </span>
          )}
        </p>
      )}
    </div>
  );
}
