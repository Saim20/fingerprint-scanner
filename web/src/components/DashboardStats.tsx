import { isDeviceOnline } from "@/lib/device-status";
import type { Device } from "@/lib/types";

export function DashboardStats({
  devices,
  peopleCount,
  eventsToday,
}: {
  devices: Device[];
  peopleCount: number;
  eventsToday: number;
}) {
  const online = devices.filter((d) => isDeviceOnline(d)).length;

  const stats = [
    { label: "Devices", value: devices.length },
    { label: "Online", value: online },
    { label: "People", value: peopleCount },
    { label: "Events today", value: eventsToday },
  ];

  return (
    <div className="grid grid-cols-2 sm:grid-cols-4 gap-4 mb-8">
      {stats.map((s) => (
        <div key={s.label} className="card text-center py-4">
          <p className="text-2xl font-semibold">{s.value}</p>
          <p className="text-sm text-[var(--muted)] mt-1">{s.label}</p>
        </div>
      ))}
    </div>
  );
}
