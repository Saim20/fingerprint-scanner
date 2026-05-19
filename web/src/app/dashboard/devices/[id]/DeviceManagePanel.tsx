"use client";

import { ConfirmButton } from "@/components/ConfirmButton";
import { deleteDevice, renameDevice } from "@/lib/actions/admin";
import { useRouter } from "next/navigation";
import { useState } from "react";

export function DeviceManagePanel({
  deviceId,
  deviceName,
}: {
  deviceId: string;
  deviceName: string;
}) {
  const router = useRouter();
  const [name, setName] = useState(deviceName);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function handleRename(e: React.FormEvent) {
    e.preventDefault();
    setSaving(true);
    setError(null);
    const result = await renameDevice(deviceId, name);
    setSaving(false);
    if (result.error) {
      setError(result.error);
      return;
    }
    router.refresh();
  }

  return (
    <div className="card mt-8 border-[var(--border)]">
      <h2 className="text-lg font-medium mb-4">Device settings</h2>

      <form onSubmit={handleRename} className="flex flex-wrap items-end gap-3 mb-6">
        <div className="flex-1 min-w-[200px]">
          <label className="label" htmlFor="device-name">
            Name
          </label>
          <input
            id="device-name"
            className="input"
            value={name}
            onChange={(e) => setName(e.target.value)}
            required
          />
        </div>
        <button type="submit" className="btn btn-primary" disabled={saving}>
          {saving ? "Saving…" : "Save name"}
        </button>
      </form>

      {error && <p className="text-red-400 text-sm mb-4">{error}</p>}

      <div className="pt-4 border-t border-[var(--border)]">
        <p className="text-sm text-[var(--muted)] mb-3">
          Deleting removes this device, its fingerprint mappings, and attendance
          history. The ESP must be re-provisioned with a new device entry.
        </p>
        <ConfirmButton
          label="Delete device"
          confirmLabel="Yes, delete device"
          pendingLabel="Deleting…"
          onConfirm={async () => {
            const result = await deleteDevice(deviceId);
            if (result && "error" in result && result.error) {
              setError(result.error);
            }
          }}
        />
      </div>
    </div>
  );
}
