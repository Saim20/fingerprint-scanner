import type { Device } from "./types";

const ONLINE_MS = 60_000;

export function isDeviceOnline(device: Device): boolean {
  if (!device.last_seen_at) return false;
  return Date.now() - new Date(device.last_seen_at).getTime() < ONLINE_MS;
}

export function hasPendingCommand(device: Device): boolean {
  return device.command_seq > device.ack_seq;
}
