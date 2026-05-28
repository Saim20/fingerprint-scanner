"use client";

import { DashboardStats } from "@/components/DashboardStats";
import { createClient } from "@/lib/supabase/client";
import { useDevicesSync } from "@/lib/hooks/use-devices-sync";
import type { Device } from "@/lib/types";
import Link from "next/link";
import { useCallback, useEffect, useState } from "react";
import { DeviceList } from "./devices/DeviceList";

export function LiveDashboard({
  initialDevices,
  peopleCount: initialPeopleCount,
  eventsToday: initialEventsToday,
}: {
  initialDevices: Device[];
  peopleCount: number;
  eventsToday: number;
}) {
  const { devices } = useDevicesSync(initialDevices);
  const [peopleCount, setPeopleCount] = useState(initialPeopleCount);
  const [eventsToday, setEventsToday] = useState(initialEventsToday);

  const refreshCounts = useCallback(async () => {
    const supabase = createClient();
    const startOfDay = new Date();
    startOfDay.setHours(0, 0, 0, 0);

    const [{ count: people }, { count: events }] = await Promise.all([
      supabase.from("people").select("*", { count: "exact", head: true }),
      supabase
        .from("attendance_events")
        .select("*", { count: "exact", head: true })
        .gte("created_at", startOfDay.toISOString()),
    ]);

    if (people != null) setPeopleCount(people);
    if (events != null) setEventsToday(events);
  }, []);

  useEffect(() => {
    setPeopleCount(initialPeopleCount);
    setEventsToday(initialEventsToday);
  }, [initialPeopleCount, initialEventsToday]);

  useEffect(() => {
    const supabase = createClient();
    const channel = supabase
      .channel("dashboard-stats")
      .on(
        "postgres_changes",
        { event: "*", schema: "public", table: "people" },
        () => void refreshCounts(),
      )
      .on(
        "postgres_changes",
        { event: "INSERT", schema: "public", table: "attendance_events" },
        () => void refreshCounts(),
      )
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, [refreshCounts]);

  return (
    <div>
      <div className="flex flex-wrap items-center justify-between gap-4 mb-6">
        <h1 className="text-2xl font-semibold">Devices</h1>
        <Link href="/dashboard/devices/new" className="btn btn-primary no-underline">
          Add device
        </Link>
      </div>

      <DashboardStats
        devices={devices}
        peopleCount={peopleCount}
        eventsToday={eventsToday}
      />

      <DeviceList devices={devices} />
    </div>
  );
}
