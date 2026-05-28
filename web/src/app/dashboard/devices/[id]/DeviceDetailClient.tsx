"use client";

import { DeviceSyncProvider } from "@/components/device/DeviceSyncContext";
import type { MappingRow } from "@/lib/mapping-rows";
import type { Device, Person } from "@/lib/types";
import { ApiKeyBanner } from "./ApiKeyBanner";
import { DeviceCommands } from "./DeviceCommands";
import { DeviceLiveStatus } from "./DeviceLiveStatus";
import { DeviceManagePanel } from "./DeviceManagePanel";
import { MappingTable } from "./MappingTable";
import { TemplateSyncPanel } from "./TemplateSyncPanel";

export function DeviceDetailClient({
  device,
  people,
  mappingRows,
  apiKey,
}: {
  device: Device;
  people: Person[];
  mappingRows: MappingRow[];
  apiKey?: string;
}) {
  return (
    <DeviceSyncProvider
      deviceId={device.id}
      initialDevice={device}
      initialMappings={mappingRows}
      people={people}
    >
      <DeviceLiveStatus />

      {apiKey && <ApiKeyBanner apiKey={apiKey} deviceId={device.id} />}

      <DeviceCommands />

      <TemplateSyncPanel />

      <section className="mt-8">
        <h2 className="text-lg font-medium mb-3">Fingerprint mappings</h2>
        <MappingTable />
      </section>

      <DeviceManagePanel deviceId={device.id} deviceName={device.name} />
    </DeviceSyncProvider>
  );
}
