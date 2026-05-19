import { AttendanceFeed } from "./AttendanceFeed";

export default function AttendancePage() {
  return (
    <div>
      <h1 className="text-2xl font-semibold mb-6">Attendance</h1>
      <p className="text-sm text-[var(--muted)] mb-4">
        Live feed of scan and enroll events from all devices.
      </p>
      <AttendanceFeed />
    </div>
  );
}
