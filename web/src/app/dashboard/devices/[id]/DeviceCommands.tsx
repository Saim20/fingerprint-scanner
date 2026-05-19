"use client";

import { setDeviceBackgroundScan } from "@/lib/actions/admin";
import { createClient } from "@/lib/supabase/client";
import type { DeviceMode, Person } from "@/lib/types";
import { useRouter } from "next/navigation";
import { useState } from "react";

export function DeviceCommands({
  deviceId,
  people,
  reportedSlots = [],
  backgroundScan = false,
}: {
  deviceId: string;
  people: Person[];
  reportedSlots?: number[];
  backgroundScan?: boolean;
}) {
  const router = useRouter();
  const [personId, setPersonId] = useState(people[0]?.id ?? "");
  const [fpSlot, setFpSlot] = useState(0);

  const selectedPerson = people.find((p) => p.id === personId);
  const preferredSlot = (() => {
    const ext = selectedPerson?.external_id?.trim();
    if (!ext) return null;
    const n = Number(ext);
    if (Number.isInteger(n) && n >= 1 && n <= 150) return n;
    return null;
  })();
  const [deleteSlot, setDeleteSlot] = useState(reportedSlots[0] ?? 1);
  const [loading, setLoading] = useState(false);
  const [bgLoading, setBgLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [sentHint, setSentHint] = useState<string | null>(null);

  async function toggleBackgroundScan(enabled: boolean) {
    setBgLoading(true);
    setError(null);
    setSentHint(null);
    const result = await setDeviceBackgroundScan(deviceId, enabled);
    setBgLoading(false);
    if (result.error) {
      setError(result.error);
      return;
    }
    setSentHint(
      enabled
        ? "Passive scan enabled — device will clock in when a finger is placed."
        : "Command mode — device only runs dashboard commands.",
    );
    router.refresh();
  }

  async function issue(
    mode: DeviceMode,
    opts?: { personId?: string; fpSlot?: number },
  ) {
    setLoading(true);
    setError(null);
    setSentHint(null);
    const supabase = createClient();
    const { error: rpcError } = await supabase.rpc("issue_device_command", {
      p_device_id: deviceId,
      p_mode: mode,
      p_person_id: opts?.personId ?? null,
      p_fp_slot: opts?.fpSlot ?? 0,
    });
    setLoading(false);
    if (rpcError) {
      setError(rpcError.message);
      return;
    }

    if (mode === "idle") {
      setSentHint("Command queue cleared on the device.");
    } else if (mode === "delete") {
      setSentHint(`Delete slot ${opts?.fpSlot ?? deleteSlot} sent — device removes it via Realtime.`);
    } else if (mode === "clear") {
      setSentHint("Clear all sent — device wipes templates via Realtime.");
    } else {
      setSentHint("Command queued — press GO on the scanner to start.");
    }
    router.refresh();
  }

  return (
    <div className="card space-y-4">
      <div>
        <h2 className="text-lg font-medium">Device control</h2>
        <p className="text-sm text-[var(--muted)] font-mono mb-2">Device ID: {deviceId}</p>
      <p className="text-sm text-[var(--muted)] mt-1">
          Enroll and test scan need GO on the scanner. Delete, clear all, and cancel run from the
          cloud automatically. Use <strong>Cancel command</strong> to clear a stuck queue.
        </p>
      </div>

      {error && <p className="text-red-400 text-sm">{error}</p>}
      {sentHint && <p className="text-amber-300 text-sm">{sentHint}</p>}

      <div className="flex flex-wrap items-center justify-between gap-3 pb-4 border-b border-[var(--border)]">
        <div>
          <p className="text-sm font-medium">Passive attendance scan</p>
          <p className="text-sm text-[var(--muted)]">
            {backgroundScan
              ? "Device continuously scans and logs attendance when a finger is placed."
              : "Command mode — only runs explicit dashboard commands (enroll, delete, test scan)."}
          </p>
        </div>
        <label className="flex items-center gap-2 cursor-pointer shrink-0">
          <input
            type="checkbox"
            className="w-4 h-4"
            checked={backgroundScan}
            disabled={bgLoading || loading}
            onChange={(e) => toggleBackgroundScan(e.target.checked)}
          />
          <span className="text-sm">{backgroundScan ? "On" : "Off"}</span>
        </label>
      </div>

      <div className="space-y-2 border-b border-[var(--border)] pb-4">
        <p className="text-sm font-medium">Enroll person</p>
        <label className="label">Person</label>
        <select
          className="input"
          value={personId}
          onChange={(e) => setPersonId(e.target.value)}
        >
          {people.length === 0 ? (
            <option value="">Create a person first</option>
          ) : (
            people.map((p) => {
              const ext = p.external_id?.trim();
              const slot =
                ext && Number.isInteger(Number(ext)) && Number(ext) >= 1 && Number(ext) <= 150
                  ? Number(ext)
                  : null;
              return (
                <option key={p.id} value={p.id}>
                  {p.display_name}
                  {slot != null ? ` → slot ${slot}` : ext ? ` (${ext})` : ""}
                </option>
              );
            })
          )}
        </select>
        <label className="label">FP slot (0 = use external ID or auto)</label>
        <input
          type="number"
          className="input"
          min={0}
          max={150}
          value={fpSlot}
          onChange={(e) => setFpSlot(Number(e.target.value))}
        />
        {preferredSlot != null && fpSlot === 0 && (
          <p className="text-xs text-[var(--muted)]">
            Will enroll on device slot <strong>{preferredSlot}</strong> (from external ID).
          </p>
        )}
        <button
          type="button"
          className="btn btn-primary"
          disabled={loading || !personId}
          onClick={() =>
            issue("add", {
              personId,
              fpSlot: fpSlot > 0 ? fpSlot : preferredSlot ?? 0,
            })
          }
        >
          Queue enroll
        </button>
      </div>

      <div className="flex flex-wrap gap-2">
        <button
          type="button"
          className="btn btn-ghost"
          disabled={loading}
          onClick={() => issue("scan")}
        >
          Queue test scan
        </button>
        <button
          type="button"
          className="btn btn-ghost"
          disabled={loading}
          onClick={() => issue("idle")}
        >
          Cancel command
        </button>
      </div>

      <div className="space-y-3 pt-2 border-t border-[var(--border)]">
        <p className="text-sm font-medium">Remove fingerprint from device</p>
        <div className="flex flex-wrap items-end gap-2">
          <div>
            <label className="label">Slot</label>
            <input
              type="number"
              className="input w-24"
              min={1}
              max={150}
              value={deleteSlot}
              onChange={(e) => setDeleteSlot(Number(e.target.value))}
            />
          </div>
          <button
            type="button"
            className="btn btn-danger"
            disabled={loading}
            onClick={() => issue("delete", { fpSlot: deleteSlot })}
          >
            Delete from device
          </button>
        </div>
        {reportedSlots.length > 0 && (
          <div className="flex flex-wrap gap-2">
            <span className="text-xs text-[var(--muted)] self-center">On device:</span>
            {reportedSlots.map((slot) => (
              <button
                key={slot}
                type="button"
                className="btn btn-ghost text-xs py-1 px-2"
                disabled={loading}
                onClick={() => issue("delete", { fpSlot: slot })}
              >
                Delete slot {slot}
              </button>
            ))}
          </div>
        )}
        {(reportedSlots.length > 0 || deleteSlot > 0) && (
          <div className="pt-2 border-t border-[var(--border)]">
            <p className="text-sm font-medium text-red-300 mb-2">Clear entire device</p>
            <button
              type="button"
              className="btn btn-danger"
              disabled={loading}
              onClick={() => {
                if (
                  !window.confirm(
                    "Remove ALL fingerprint templates from this device? Cloud mappings will be cleared too.",
                  )
                ) {
                  return;
                }
                issue("clear");
              }}
            >
              Clear all fingerprints
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
