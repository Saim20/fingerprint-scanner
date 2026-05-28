"use client";

import { createClient } from "@/lib/supabase/client";
import { toMappingRows, type MappingRow } from "@/lib/mapping-rows";
import { enrollCommandFinished, hasPendingCommand } from "@/lib/device-status";
import type { Device } from "@/lib/types";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";

export function useDeviceSync(
  deviceId: string,
  initialDevice: Device,
  initialMappings: MappingRow[],
) {
  const [device, setDevice] = useState(initialDevice);
  const [mappings, setMappings] = useState(initialMappings);
  const deviceRef = useRef(device);
  deviceRef.current = device;

  useEffect(() => {
    setDevice(initialDevice);
  }, [initialDevice]);

  useEffect(() => {
    setMappings(initialMappings);
  }, [initialMappings]);

  const fetchDevice = useCallback(async () => {
    const supabase = createClient();
    const { data } = await supabase.from("devices").select("*").eq("id", deviceId).single();
    if (data) setDevice(data as Device);
  }, [deviceId]);

  const fetchMappings = useCallback(async () => {
    const supabase = createClient();
    const { data } = await supabase
      .from("fp_mappings")
      .select("fp_slot, person_id, enrolled_at, people(display_name, external_id)")
      .eq("device_id", deviceId)
      .order("fp_slot");
    setMappings(toMappingRows(data));
  }, [deviceId]);

  useEffect(() => {
    const supabase = createClient();

    const channel = supabase
      .channel(`device-sync-${deviceId}`)
      .on(
        "postgres_changes",
        {
          event: "UPDATE",
          schema: "public",
          table: "devices",
          filter: `id=eq.${deviceId}`,
        },
        (payload) => {
          if (payload.new) {
            const next = payload.new as Device;
            const prev = deviceRef.current;
            setDevice(next);
            if (enrollCommandFinished(prev, next)) {
              void fetchMappings();
            }
          } else {
            void fetchDevice();
          }
        },
      )
      .on(
        "postgres_changes",
        {
          event: "*",
          schema: "public",
          table: "fp_mappings",
          filter: `device_id=eq.${deviceId}`,
        },
        () => {
          void fetchMappings();
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, [deviceId, fetchDevice, fetchMappings]);

  useEffect(() => {
    if (!hasPendingCommand(device) || device.desired_mode !== "add") return;

    const interval = setInterval(() => {
      void fetchMappings();
      void fetchDevice();
    }, 2000);

    return () => clearInterval(interval);
  }, [
    device.command_seq,
    device.ack_seq,
    device.desired_mode,
    fetchDevice,
    fetchMappings,
  ]);

  const reportedSlots = useMemo(
    () => (device.reported_fp_slots ?? []).map(Number).sort((a, b) => a - b),
    [device.reported_fp_slots],
  );

  return {
    device,
    mappings,
    reportedSlots,
    refreshDevice: fetchDevice,
    refreshMappings: fetchMappings,
  };
}
