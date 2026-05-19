"use client";

import { createClient } from "@/lib/supabase/client";
import { hasPendingCommand, isDeviceOnline } from "@/lib/device-status";
import type { Device, DeviceMode, Person } from "@/lib/types";
import { useRouter } from "next/navigation";
import { useEffect, useState } from "react";

function pendingMessage(device: Device, people: Person[]): string | null {
  if (!hasPendingCommand(device)) return null;

  const mode = device.desired_mode as DeviceMode;
  const slot = device.desired_fp_slot;
  const person = people.find((p) => p.id === device.desired_person_id);

  switch (mode) {
    case "add":
      return person
        ? slot > 0
          ? `Enroll ${person.display_name} on slot ${slot} — press GO on the device`
          : `Enroll ${person.display_name} — press GO on the device`
        : slot > 0
          ? `Enroll queued on slot ${slot} — press GO on the device`
          : "Enroll queued — press GO on the device";
    case "scan":
      return "Test scan queued — press GO on the device";
    case "delete":
      return slot > 0
        ? `Deleting slot ${slot} on device…`
        : "Delete queued — running on device…";
    case "clear":
      return "Clearing all fingerprints on device…";
    default:
      return "Command pending — press GO on the device";
  }
}

export function DeviceLiveStatus({
  initialDevice,
  people,
}: {
  initialDevice: Device;
  people: Person[];
}) {
  const router = useRouter();
  const [device, setDevice] = useState(initialDevice);

  useEffect(() => {
    setDevice(initialDevice);
  }, [initialDevice]);

  useEffect(() => {
    const supabase = createClient();

    async function reload() {
      const { data } = await supabase
        .from("devices")
        .select("*")
        .eq("id", initialDevice.id)
        .single();
      if (data) {
        setDevice(data as Device);
      }
    }

    const channel = supabase
      .channel(`device-${initialDevice.id}`)
      .on(
        "postgres_changes",
        {
          event: "UPDATE",
          schema: "public",
          table: "devices",
          filter: `id=eq.${initialDevice.id}`,
        },
        () => {
          reload();
          router.refresh();
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, [initialDevice.id, router]);

  const online = isDeviceOnline(device);
  const pending = hasPendingCommand(device);
  const pendingText = pendingMessage(device, people);
  const reportedSlots = (device.reported_fp_slots ?? []).map(Number).sort((a, b) => a - b);

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
          <p className="text-[var(--muted)] mt-1">
            {device.desired_mode === "delete" || device.desired_mode === "clear"
              ? "The device receives this instantly via Supabase Realtime."
              : "Press the GO button (GPIO0) on the scanner to start enroll or test scan."}
          </p>
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
