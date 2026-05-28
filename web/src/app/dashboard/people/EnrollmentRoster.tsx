"use client";

import { createClient } from "@/lib/supabase/client";
import {
  commandFinished,
  commandNeedsGo,
  enrollCommandFinished,
  hasPendingCommand,
  isDeviceOnline,
} from "@/lib/device-status";
import { externalIdKey, sortPeopleByExternalId } from "@/lib/sort-people";
import type { Device, Person } from "@/lib/types";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";

type DeviceOption = { id: string; name: string };

export function EnrollmentRoster({
  people,
  devices,
}: {
  people: Person[];
  devices: DeviceOption[];
}) {
  const [deviceId, setDeviceId] = useState(devices[0]?.id ?? "");
  const [enrolledPersonIds, setEnrolledPersonIds] = useState<Set<string>>(new Set());
  const [device, setDevice] = useState<Device | null>(null);
  const deviceRef = useRef<Device | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [hint, setHint] = useState<string | null>(null);

  const rosterPeople = useMemo(
    () => sortPeopleByExternalId(people.filter((p) => externalIdKey(p.external_id))),
    [people],
  );

  const loadMappings = useCallback(async (id: string) => {
    const supabase = createClient();
    const { data } = await supabase
      .from("fp_mappings")
      .select("person_id")
      .eq("device_id", id);
    setEnrolledPersonIds(new Set((data ?? []).map((m) => m.person_id)));
  }, []);

  const loadDevice = useCallback(async (id: string) => {
    const supabase = createClient();
    const { data } = await supabase.from("devices").select("*").eq("id", id).single();
    if (data) {
      const d = data as Device;
      deviceRef.current = d;
      setDevice(d);
    }
  }, []);

  useEffect(() => {
    deviceRef.current = device;
  }, [device]);

  useEffect(() => {
    if (!deviceId) {
      setEnrolledPersonIds(new Set());
      setDevice(null);
      deviceRef.current = null;
      return;
    }
    loadMappings(deviceId);
    loadDevice(deviceId);
  }, [deviceId, loadMappings, loadDevice]);

  useEffect(() => {
    if (!deviceId) return;

    const supabase = createClient();

    const mappingsChannel = supabase
      .channel(`roster-mappings-${deviceId}`)
      .on(
        "postgres_changes",
        {
          event: "*",
          schema: "public",
          table: "fp_mappings",
          filter: `device_id=eq.${deviceId}`,
        },
        () => {
          void loadMappings(deviceId);
        },
      )
      .subscribe();

    const deviceChannel = supabase
      .channel(`roster-device-${deviceId}`)
      .on(
        "postgres_changes",
        {
          event: "UPDATE",
          schema: "public",
          table: "devices",
          filter: `id=eq.${deviceId}`,
        },
        (payload) => {
          if (payload.new) {
            const prev = deviceRef.current;
            const next = payload.new as Device;
            deviceRef.current = next;
            setDevice(next);
            if (prev && commandFinished(prev, next)) {
              setHint(null);
              if (enrollCommandFinished(prev, next)) {
                void loadMappings(deviceId);
              }
            } else if (prev && commandNeedsGo(prev.desired_mode) && !commandNeedsGo(next.desired_mode)) {
              setHint(null);
            }
          } else {
            void loadDevice(deviceId);
          }
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(mappingsChannel);
      supabase.removeChannel(deviceChannel);
    };
  }, [deviceId, loadMappings, loadDevice]);

  useEffect(() => {
    if (!deviceId || !device) return;
    if (!hasPendingCommand(device) || device.desired_mode !== "add") return;

    const interval = setInterval(() => {
      void loadMappings(deviceId);
      void loadDevice(deviceId);
    }, 2000);

    return () => clearInterval(interval);
  }, [deviceId, device, loadMappings, loadDevice]);

  useEffect(() => {
    if (!device) return;
    if (!hasPendingCommand(device)) {
      setHint(null);
      return;
    }
    if (!commandNeedsGo(device.desired_mode)) {
      setHint(null);
    }
  }, [device]);

  const online = device ? isDeviceOnline(device) : false;
  const pending = device ? hasPendingCommand(device) : false;
  const pendingEnrollPersonId =
    pending && device?.desired_mode === "add" ? device.desired_person_id : null;

  async function enroll(person: Person) {
    if (!deviceId || !device) return;
    setLoading(true);
    setError(null);
    setHint(null);

    const supabase = createClient();
    const { error: rpcError } = await supabase.rpc("issue_device_command", {
      p_device_id: deviceId,
      p_mode: "add",
      p_person_id: person.id,
      p_fp_slot: 0,
    });

    setLoading(false);
    if (rpcError) {
      setError(rpcError.message);
      return;
    }

    setHint(`Enroll queued for ${person.display_name} — press GO on the scanner.`);
    await loadDevice(deviceId);
  }

  if (devices.length === 0) {
    return (
      <div className="card mb-8">
        <h2 className="font-medium mb-2">Enrollment roster</h2>
        <p className="text-sm text-[var(--muted)]">
          Add a device first to enroll people from this list.
        </p>
      </div>
    );
  }

  return (
    <div className="card mb-8 space-y-4">
      <div>
        <h2 className="font-medium">Enrollment roster</h2>
        <p className="text-sm text-[var(--muted)] mt-1">
          Sorted by ID. Fingerprint slots are assigned automatically on the device.
        </p>
      </div>

      <div>
        <label className="label" htmlFor="roster_device">
          Device
        </label>
        <select
          id="roster_device"
          className="input max-w-md"
          value={deviceId}
          onChange={(e) => setDeviceId(e.target.value)}
        >
          {devices.map((d) => (
            <option key={d.id} value={d.id}>
              {d.name}
            </option>
          ))}
        </select>
        {device && (
          <p className="text-xs text-[var(--muted)] mt-1">
            <span className={online ? "text-green-400" : "text-red-400"}>
              {online ? "Online" : "Offline"}
            </span>
            {pending && " · Command pending"}
          </p>
        )}
      </div>

      {error && <p className="text-red-400 text-sm">{error}</p>}
      {hint && device && commandNeedsGo(device.desired_mode) && (
        <p className="text-amber-300 text-sm">{hint}</p>
      )}

      {rosterPeople.length === 0 ? (
        <p className="text-sm text-[var(--muted)]">
          No people with an ID yet. Import a CSV or add an external ID when creating a person.
        </p>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
                <th className="py-3 pr-4">ID</th>
                <th className="py-3 pr-4">Name</th>
                <th className="py-3 pr-4">Status</th>
                <th className="py-3 text-right">Action</th>
              </tr>
            </thead>
            <tbody>
              {rosterPeople.map((person) => {
                const id = externalIdKey(person.external_id)!;
                const enrolled = enrolledPersonIds.has(person.id);
                const enrolling = pendingEnrollPersonId === person.id;
                const otherPending = pending && !enrolling;

                let status = "Not enrolled";
                if (enrolled) status = "Enrolled";
                else if (enrolling) status = "Enrolling…";

                const enrollDisabled =
                  loading ||
                  enrolled ||
                  !online ||
                  otherPending ||
                  enrolling;

                return (
                  <tr key={person.id} className="border-b border-[var(--border)]">
                    <td className="py-3 pr-4 font-mono text-xs">{id}</td>
                    <td className="py-3 pr-4">{person.display_name}</td>
                    <td className="py-3 pr-4 text-[var(--muted)]">{status}</td>
                    <td className="py-3 text-right">
                      {enrolled ? (
                        <span className="text-xs text-[var(--muted)]">—</span>
                      ) : (
                        <button
                          type="button"
                          className="btn btn-primary text-xs py-1 px-3"
                          disabled={enrollDisabled}
                          title={
                            !online
                              ? "Device offline"
                              : otherPending
                                ? "Another command is pending"
                                : undefined
                          }
                          onClick={() => enroll(person)}
                        >
                          {enrolling ? "Queued…" : "Enroll"}
                        </button>
                      )}
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
