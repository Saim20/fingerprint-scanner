"use client";

import { createClient } from "@/lib/supabase/client";
import type { DeviceMode, Person } from "@/lib/types";
import { useRouter } from "next/navigation";
import { useState } from "react";

export function DeviceCommands({
  deviceId,
  people,
}: {
  deviceId: string;
  people: Person[];
}) {
  const router = useRouter();
  const [personId, setPersonId] = useState(people[0]?.id ?? "");
  const [fpSlot, setFpSlot] = useState(0);
  const [deleteSlot, setDeleteSlot] = useState(1);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function issue(
    mode: DeviceMode,
    opts?: { personId?: string; fpSlot?: number },
  ) {
    setLoading(true);
    setError(null);
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
    router.refresh();
  }

  return (
    <div className="card space-y-4">
      <h2 className="text-lg font-medium">Remote commands</h2>
      {error && <p className="text-red-400 text-sm">{error}</p>}

      <div className="space-y-2 border-b border-[var(--border)] pb-4">
        <p className="text-sm font-medium">Enroll (add mode)</p>
        <label className="label">Person</label>
        <select
          className="input"
          value={personId}
          onChange={(e) => setPersonId(e.target.value)}
        >
          {people.length === 0 ? (
            <option value="">Create a person first</option>
          ) : (
            people.map((p) => (
              <option key={p.id} value={p.id}>
                {p.display_name}
              </option>
            ))
          )}
        </select>
        <label className="label">FP slot (0 = auto)</label>
        <input
          type="number"
          className="input"
          min={0}
          max={150}
          value={fpSlot}
          onChange={(e) => setFpSlot(Number(e.target.value))}
        />
        <button
          type="button"
          className="btn btn-primary"
          disabled={loading || !personId}
          onClick={() => issue("add", { personId, fpSlot })}
        >
          Start remote enroll
        </button>
      </div>

      <div className="flex flex-wrap gap-2">
        <button
          type="button"
          className="btn btn-ghost"
          disabled={loading}
          onClick={() => issue("scan")}
        >
          Scan mode
        </button>
        <button
          type="button"
          className="btn btn-ghost"
          disabled={loading}
          onClick={() => issue("idle")}
        >
          Set idle
        </button>
      </div>

      <div className="flex flex-wrap items-end gap-2 pt-2 border-t border-[var(--border)]">
        <div>
          <label className="label">Delete slot</label>
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
          Delete template
        </button>
      </div>
    </div>
  );
}
