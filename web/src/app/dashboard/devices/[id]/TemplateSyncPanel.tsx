"use client";

import { useDeviceSyncContext } from "@/components/device/DeviceSyncContext";
import { ConfirmButton } from "@/components/ConfirmButton";
import { assignMapping, clearStaleMappings } from "@/lib/actions/admin";
import { externalIdKey } from "@/lib/sort-people";
import { useMemo, useState } from "react";

function DriftAlerts({
  unmappedSlots,
  staleSlots,
  deviceId,
  setError,
  onStaleCleared,
}: {
  unmappedSlots: number[];
  staleSlots: number[];
  deviceId: string;
  setError: (s: string | null) => void;
  onStaleCleared: () => void;
}) {
  return (
    <div className="space-y-3 text-sm">
      {unmappedSlots.length > 0 && (
        <div>
          <p className="text-amber-400 font-medium">
            Unmapped on device ({unmappedSlots.length})
          </p>
          <p className="text-[var(--muted)]">
            Slots {unmappedSlots.join(", ")} have templates but no person in the cloud.
          </p>
        </div>
      )}
      {staleSlots.length > 0 && (
        <div className="flex flex-wrap items-center gap-2">
          <div>
            <p className="text-amber-400 font-medium">
              Stale in cloud ({staleSlots.length})
            </p>
            <p className="text-[var(--muted)]">
              Slots {staleSlots.join(", ")} are mapped here but not reported on the device.
            </p>
          </div>
          <ConfirmButton
            label="Clear stale mappings"
            confirmLabel="Clear"
            variant="ghost"
            onConfirm={async () => {
              setError(null);
              const result = await clearStaleMappings(deviceId, staleSlots);
              if (result.error) {
                setError(result.error);
                return;
              }
              onStaleCleared();
            }}
          />
        </div>
      )}
    </div>
  );
}

export function TemplateSyncPanel() {
  const { deviceId, device, mappings, reportedSlots, people, refreshMappings } =
    useDeviceSyncContext();
  const [error, setError] = useState<string | null>(null);
  const [assignSlot, setAssignSlot] = useState<number | "">("");
  const [assignPerson, setAssignPerson] = useState(people[0]?.id ?? "");
  const [loading, setLoading] = useState(false);

  const lastSyncAt = device.last_template_sync_at ?? null;
  const reportedCount = device.reported_fp_count ?? null;

  const mappedSlots = useMemo(
    () => new Set(mappings.map((m) => m.fp_slot)),
    [mappings],
  );

  const unmappedSlots = useMemo(
    () => reportedSlots.filter((s) => !mappedSlots.has(s)),
    [reportedSlots, mappedSlots],
  );

  const staleSlots = useMemo(
    () => mappings.map((m) => m.fp_slot).filter((s) => !reportedSlots.includes(s)),
    [mappings, reportedSlots],
  );

  const hasDrift = unmappedSlots.length > 0 || staleSlots.length > 0;
  const deviceEmpty = reportedSlots.length === 0 && (reportedCount ?? 0) === 0;

  async function handleAssign(e: React.FormEvent) {
    e.preventDefault();
    if (assignSlot === "" || !assignPerson) return;
    setLoading(true);
    setError(null);
    const result = await assignMapping(deviceId, assignSlot, assignPerson);
    setLoading(false);
    if (result.error) {
      setError(result.error);
      return;
    }
    await refreshMappings();
  }

  return (
    <section className="card mt-8 space-y-4">
      <h2 className="text-lg font-medium">Template sync</h2>
      <p className="text-sm text-[var(--muted)]">
        Device reports occupied slots on each cloud poll. Map unmapped slots to people;
        clear stale rows when the template was removed on the scanner.
        {lastSyncAt && (
          <> Last sync {new Date(lastSyncAt).toLocaleString()}.</>
        )}
      </p>

      {error && <p className="text-red-400 text-sm">{error}</p>}

      {deviceEmpty && !mappings.length && (
        <p className="text-sm text-[var(--muted)]">
          Waiting for device sync — ensure WiFi and cloud are configured.
        </p>
      )}

      {!deviceEmpty && (
        <p className="text-sm">
          <span className="text-[var(--muted)]">On device:</span>{" "}
          {reportedSlots.length
            ? reportedSlots.join(", ")
            : reportedCount != null
              ? `${reportedCount} template(s)`
              : "—"}
        </p>
      )}

      {hasDrift ? (
        <DriftAlerts
          unmappedSlots={unmappedSlots}
          staleSlots={staleSlots}
          deviceId={deviceId}
          setError={setError}
          onStaleCleared={() => refreshMappings()}
        />
      ) : (
        reportedSlots.length > 0 && (
          <p className="text-sm text-green-400">Cloud mappings match device templates.</p>
        )
      )}

      <form
        onSubmit={handleAssign}
        className="flex flex-wrap items-end gap-3 pt-2 border-t border-[var(--border)]"
      >
        <div>
          <label className="label">Link slot → person</label>
          <input
            type="number"
            className="input w-24"
            min={1}
            max={150}
            value={assignSlot}
            onChange={(e) =>
              setAssignSlot(e.target.value === "" ? "" : Number(e.target.value))
            }
            required
          />
        </div>
        <div>
          <label className="label">Person</label>
          <select
            className="input"
            value={assignPerson}
            onChange={(e) => setAssignPerson(e.target.value)}
          >
            {people.map((p) => {
              const id = externalIdKey(p.external_id);
              return (
                <option key={p.id} value={p.id}>
                  {p.display_name}
                  {id ? ` — ${id}` : ""}
                </option>
              );
            })}
          </select>
        </div>
        <button type="submit" className="btn btn-primary" disabled={loading || !assignPerson}>
          Assign mapping
        </button>
      </form>

      <p className="text-xs text-[var(--muted)]">
        Tip: use <strong>External ID</strong> as a string label (e.g. EMP-042) for roster sorting.
        Fingerprint slots are assigned automatically on the device when enrolling.
      </p>
    </section>
  );
}
