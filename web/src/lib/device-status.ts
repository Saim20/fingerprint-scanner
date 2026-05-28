import type { Device, DeviceMode } from "./types";

const ONLINE_MS = 60_000;

export function isDeviceOnline(device: Device): boolean {
  if (!device.last_seen_at) return false;
  return Date.now() - new Date(device.last_seen_at).getTime() < ONLINE_MS;
}

export function hasPendingCommand(device: Device): boolean {
  return device.command_seq > device.ack_seq;
}

/** Only enroll and test scan wait for GPIO0 GO; delete/clear/cancel run from the cloud. */
export function commandNeedsGo(mode: DeviceMode | string): boolean {
  return mode === "add" || mode === "scan";
}

/** Pending command was acknowledged (device finished or idle). */
export function commandFinished(prev: Device, next: Device): boolean {
  return hasPendingCommand(prev) && !hasPendingCommand(next);
}

/** True when an enroll command was pending and the device has now acknowledged it. */
export function enrollCommandFinished(prev: Device, next: Device): boolean {
  if (prev.desired_mode !== "add") return false;
  return commandFinished(prev, next);
}
