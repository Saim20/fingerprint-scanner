"use client";

import { useDeviceSync } from "@/lib/hooks/use-device-sync";
import type { MappingRow } from "@/lib/mapping-rows";
import type { Device, Person } from "@/lib/types";
import { createContext, useContext, type ReactNode } from "react";

type DeviceSyncValue = ReturnType<typeof useDeviceSync> & {
  deviceId: string;
  people: Person[];
};

const DeviceSyncContext = createContext<DeviceSyncValue | null>(null);

export function DeviceSyncProvider({
  deviceId,
  initialDevice,
  initialMappings,
  people,
  children,
}: {
  deviceId: string;
  initialDevice: Device;
  initialMappings: MappingRow[];
  people: Person[];
  children: ReactNode;
}) {
  const sync = useDeviceSync(deviceId, initialDevice, initialMappings);

  return (
    <DeviceSyncContext.Provider value={{ ...sync, deviceId, people }}>
      {children}
    </DeviceSyncContext.Provider>
  );
}

export function useDeviceSyncContext(): DeviceSyncValue {
  const ctx = useContext(DeviceSyncContext);
  if (!ctx) {
    throw new Error("useDeviceSyncContext must be used within DeviceSyncProvider");
  }
  return ctx;
}
