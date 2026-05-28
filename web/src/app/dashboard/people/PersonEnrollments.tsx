"use client";

import { createClient } from "@/lib/supabase/client";
import Link from "next/link";
import { useCallback, useEffect, useState } from "react";

type EnrollmentRow = {
  fp_slot: number;
  enrolled_at: string;
  device_id: string;
  deviceName: string;
};

function deviceFields(
  devices: { name: string; id?: string } | { name: string; id?: string }[] | null,
): { name: string; id: string } | null {
  if (!devices) return null;
  const d = Array.isArray(devices) ? devices[0] : devices;
  if (!d) return null;
  return { name: d.name, id: d.id ?? "" };
}

export function PersonEnrollments({ personId }: { personId: string }) {
  const [rows, setRows] = useState<EnrollmentRow[]>([]);

  const fetchRows = useCallback(async () => {
    const supabase = createClient();
    const { data } = await supabase
      .from("fp_mappings")
      .select("fp_slot, enrolled_at, device_id, devices(id, name)")
      .eq("person_id", personId);

    setRows(
      (data ?? []).map((m) => {
        const dev = deviceFields(m.devices);
        return {
          fp_slot: m.fp_slot,
          enrolled_at: m.enrolled_at,
          device_id: dev?.id || m.device_id,
          deviceName: dev?.name ?? "device",
        };
      }),
    );
  }, [personId]);

  useEffect(() => {
    void fetchRows();
  }, [fetchRows]);

  useEffect(() => {
    const supabase = createClient();
    const channel = supabase
      .channel(`person-mappings-${personId}`)
      .on(
        "postgres_changes",
        {
          event: "*",
          schema: "public",
          table: "fp_mappings",
          filter: `person_id=eq.${personId}`,
        },
        () => {
          void fetchRows();
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, [personId, fetchRows]);

  if (!rows.length) {
    return <p className="text-[var(--muted)] text-sm">Not enrolled on any device.</p>;
  }

  return (
    <ul className="space-y-2">
      {rows.map((m) => (
        <li
          key={`${m.device_id}-${m.fp_slot}`}
          className="card text-sm flex justify-between items-center gap-4"
        >
          <span>
            Slot {m.fp_slot} on{" "}
            <Link href={`/dashboard/devices/${m.device_id}`} className="font-medium">
              {m.deviceName}
            </Link>
            {" · "}
            {new Date(m.enrolled_at).toLocaleString()}
          </span>
          <Link
            href={`/dashboard/devices/${m.device_id}`}
            className="btn btn-ghost text-xs no-underline shrink-0"
          >
            Manage device
          </Link>
        </li>
      ))}
    </ul>
  );
}
