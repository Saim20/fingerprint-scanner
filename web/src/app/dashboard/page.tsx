import { DashboardStats } from "@/components/DashboardStats";
import { createClient } from "@/lib/supabase/server";
import type { Device } from "@/lib/types";
import Link from "next/link";
import { DeviceList } from "./devices/DeviceList";

export default async function DashboardPage() {
  const supabase = await createClient();

  const startOfDay = new Date();
  startOfDay.setHours(0, 0, 0, 0);

  const [{ data: devices }, { count: peopleCount }, { count: eventsToday }] =
    await Promise.all([
      supabase.from("devices").select("*").order("name"),
      supabase.from("people").select("*", { count: "exact", head: true }),
      supabase
        .from("attendance_events")
        .select("*", { count: "exact", head: true })
        .gte("created_at", startOfDay.toISOString()),
    ]);

  const deviceList = (devices ?? []) as Device[];

  return (
    <div>
      <div className="flex flex-wrap items-center justify-between gap-4 mb-6">
        <h1 className="text-2xl font-semibold">Devices</h1>
        <Link href="/dashboard/devices/new" className="btn btn-primary no-underline">
          Add device
        </Link>
      </div>

      <DashboardStats
        devices={deviceList}
        peopleCount={peopleCount ?? 0}
        eventsToday={eventsToday ?? 0}
      />

      <DeviceList devices={deviceList} />
    </div>
  );
}
