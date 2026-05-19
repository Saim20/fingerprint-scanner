"use client";

import { createClient } from "@/lib/supabase/client";
import type { AttendanceEvent } from "@/lib/types";
import { useEffect, useState } from "react";

export function AttendanceFeed() {
  const [events, setEvents] = useState<AttendanceEvent[]>([]);

  useEffect(() => {
    const supabase = createClient();

    async function load() {
      const { data } = await supabase
        .from("attendance_events")
        .select(
          "id, device_id, fp_slot, person_id, event_type, created_at, devices(name), people(display_name)",
        )
        .order("created_at", { ascending: false })
        .limit(50);
      if (data) setEvents(data as unknown as AttendanceEvent[]);
    }

    load();

    const channel = supabase
      .channel("attendance")
      .on(
        "postgres_changes",
        { event: "INSERT", schema: "public", table: "attendance_events" },
        () => {
          load();
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, []);

  if (!events.length) {
    return <p className="text-[var(--muted)]">No events yet.</p>;
  }

  return (
    <table className="w-full text-sm">
      <thead>
        <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
          <th className="py-2">Time</th>
          <th className="py-2">Type</th>
          <th className="py-2">Device</th>
          <th className="py-2">Slot</th>
          <th className="py-2">Person</th>
        </tr>
      </thead>
      <tbody>
        {events.map((e) => (
          <tr key={e.id} className="border-b border-[var(--border)]">
            <td className="py-2 text-[var(--muted)]">
              {new Date(e.created_at).toLocaleString()}
            </td>
            <td className="py-2">{e.event_type}</td>
            <td className="py-2">
              {(e.devices as { name: string } | undefined)?.name ?? e.device_id.slice(0, 8)}
            </td>
            <td className="py-2">{e.fp_slot}</td>
            <td className="py-2">
              {(e.people as { display_name: string } | null)?.display_name ??
                (e.person_id ? e.person_id.slice(0, 8) : "—")}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
