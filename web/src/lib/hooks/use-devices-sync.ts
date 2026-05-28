"use client";

import { createClient } from "@/lib/supabase/client";
import type { Device } from "@/lib/types";
import { useCallback, useEffect, useState } from "react";

export function useDevicesSync(initialDevices: Device[]) {
  const [devices, setDevices] = useState(initialDevices);

  useEffect(() => {
    setDevices(initialDevices);
  }, [initialDevices]);

  const fetchDevices = useCallback(async () => {
    const supabase = createClient();
    const { data } = await supabase.from("devices").select("*").order("name");
    if (data) setDevices(data as Device[]);
  }, []);

  useEffect(() => {
    const supabase = createClient();

    const channel = supabase
      .channel("dashboard-devices")
      .on(
        "postgres_changes",
        {
          event: "*",
          schema: "public",
          table: "devices",
        },
        () => {
          void fetchDevices();
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, [fetchDevices]);

  return { devices, refreshDevices: fetchDevices };
}
