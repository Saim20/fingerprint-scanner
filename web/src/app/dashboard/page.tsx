import { createClient } from "@/lib/supabase/server";
import type { Device } from "@/lib/types";
import { LiveDashboard } from "./LiveDashboard";

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

  return (
    <LiveDashboard
      initialDevices={(devices ?? []) as Device[]}
      peopleCount={peopleCount ?? 0}
      eventsToday={eventsToday ?? 0}
    />
  );
}
