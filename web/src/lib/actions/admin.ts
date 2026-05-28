"use server";

import { createClient } from "@/lib/supabase/server";
import { revalidatePath } from "next/cache";
import { redirect } from "next/navigation";

export async function renameDevice(deviceId: string, name: string) {
  const trimmed = name.trim();
  if (!trimmed) return { error: "Name is required" };

  const supabase = await createClient();
  const { error } = await supabase
    .from("devices")
    .update({ name: trimmed })
    .eq("id", deviceId);

  if (error) return { error: error.message };
  revalidatePath("/dashboard");
  revalidatePath(`/dashboard/devices/${deviceId}`);
  return { ok: true };
}

export async function deleteDevice(deviceId: string) {
  const supabase = await createClient();
  const { error } = await supabase.from("devices").delete().eq("id", deviceId);

  if (error) return { error: error.message };
  revalidatePath("/dashboard");
  revalidatePath("/dashboard/people");
  revalidatePath("/dashboard/attendance");
  redirect("/dashboard");
}

export async function updatePerson(
  personId: string,
  display_name: string,
  external_id: string | null,
) {
  const name = display_name.trim();
  if (!name) return { error: "Display name is required" };

  const supabase = await createClient();
  const { error } = await supabase
    .from("people")
    .update({
      display_name: name,
      external_id: external_id?.trim() || null,
    })
    .eq("id", personId);

  if (error) return { error: error.message };
  revalidatePath("/dashboard/people");
  revalidatePath(`/dashboard/people/${personId}`);
  return { ok: true };
}

export async function deletePerson(personId: string) {
  const supabase = await createClient();

  const { data: mappings, error: fetchError } = await supabase
    .from("fp_mappings")
    .select("device_id, fp_slot")
    .eq("person_id", personId);

  if (fetchError) return { error: fetchError.message };

  const commandErrors: string[] = [];
  for (const m of mappings ?? []) {
    const { error: rpcError } = await supabase.rpc("issue_device_command", {
      p_device_id: m.device_id,
      p_mode: "delete",
      p_person_id: null,
      p_fp_slot: m.fp_slot,
    });
    if (rpcError) {
      commandErrors.push(`slot ${m.fp_slot}: ${rpcError.message}`);
    }
  }

  if (commandErrors.length > 0) {
    return {
      error: `Could not queue fingerprint removal on device: ${commandErrors.join("; ")}`,
    };
  }

  const { error } = await supabase.from("people").delete().eq("id", personId);

  if (error) return { error: error.message };

  for (const m of mappings ?? []) {
    revalidatePath(`/dashboard/devices/${m.device_id}`);
  }
  revalidatePath("/dashboard");
  revalidatePath("/dashboard/people");
  revalidatePath("/dashboard/attendance");
  redirect("/dashboard/people");
}

export async function assignMapping(
  deviceId: string,
  fpSlot: number,
  personId: string,
) {
  if (fpSlot < 1 || fpSlot > 150) {
    return { error: "Slot must be 1–150" };
  }

  const supabase = await createClient();
  const { error } = await supabase.rpc("assign_fp_mapping", {
    p_device_id: deviceId,
    p_fp_slot: fpSlot,
    p_person_id: personId,
  });

  if (error) return { error: error.message };
  revalidatePath(`/dashboard/devices/${deviceId}`);
  revalidatePath("/dashboard/people");
  return { ok: true };
}

export async function clearStaleMappings(deviceId: string, fpSlots: number[]) {
  if (!fpSlots.length) return { ok: true };

  const supabase = await createClient();
  const { error } = await supabase
    .from("fp_mappings")
    .delete()
    .eq("device_id", deviceId)
    .in("fp_slot", fpSlots);

  if (error) return { error: error.message };
  revalidatePath(`/dashboard/devices/${deviceId}`);
  return { ok: true };
}

/** Queue fingerprint removal on the device; cloud mapping clears when device confirms. */
export async function queueDeviceSlotDelete(deviceId: string, fpSlot: number) {
  if (fpSlot < 1 || fpSlot > 150) {
    return { error: "Slot must be 1–150" };
  }

  const supabase = await createClient();
  const { error } = await supabase.rpc("issue_device_command", {
    p_device_id: deviceId,
    p_mode: "delete",
    p_person_id: null,
    p_fp_slot: fpSlot,
  });

  if (error) return { error: error.message };
  revalidatePath(`/dashboard/devices/${deviceId}`);
  revalidatePath("/dashboard/people");
  return { ok: true };
}

/** @deprecated Use queueDeviceSlotDelete — cloud-only delete desyncs from device. */
export async function deleteMapping(deviceId: string, fpSlot: number) {
  return queueDeviceSlotDelete(deviceId, fpSlot);
}

export type CsvPersonRow = {
  external_id: string;
  display_name: string;
};

export async function importPeopleFromCsv(rows: CsvPersonRow[]) {
  if (!rows.length) {
    return { error: "No rows to import" };
  }

  const supabase = await createClient();
  const externalIds = rows.map((r) => r.external_id);

  const { data: existingRows, error: lookupError } = await supabase
    .from("people")
    .select("id, external_id")
    .in("external_id", externalIds);

  if (lookupError) return { error: lookupError.message };

  const existingByExternalId = new Map(
    (existingRows ?? [])
      .filter((p) => p.external_id)
      .map((p) => [p.external_id as string, p.id as string]),
  );

  const errors: string[] = [];
  let created = 0;
  let updated = 0;

  for (const row of rows) {
    const existingId = existingByExternalId.get(row.external_id);

    if (existingId) {
      const { error } = await supabase
        .from("people")
        .update({ display_name: row.display_name })
        .eq("id", existingId);

      if (error) {
        errors.push(`${row.external_id}: ${error.message}`);
        continue;
      }
      updated++;
    } else {
      const { error } = await supabase.from("people").insert({
        external_id: row.external_id,
        display_name: row.display_name,
      });

      if (error) {
        errors.push(`${row.external_id}: ${error.message}`);
        continue;
      }
      created++;
      existingByExternalId.set(row.external_id, "new");
    }
  }

  revalidatePath("/dashboard/people");
  revalidatePath("/dashboard");

  return {
    ok: true as const,
    created,
    updated,
    failed: errors.length,
    errors: errors.slice(0, 20),
  };
}

export async function setDeviceBackgroundScan(deviceId: string, enabled: boolean) {
  const supabase = await createClient();
  const { error } = await supabase
    .from("devices")
    .update({ background_scan: enabled })
    .eq("id", deviceId);

  if (error) return { error: error.message };
  revalidatePath(`/dashboard/devices/${deviceId}`);
  revalidatePath("/dashboard");
  return { ok: true };
}
