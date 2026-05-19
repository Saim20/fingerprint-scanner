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
  const { error } = await supabase.from("people").delete().eq("id", personId);

  if (error) return { error: error.message };
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

export async function deleteMapping(deviceId: string, fpSlot: number) {
  const supabase = await createClient();
  const { error } = await supabase
    .from("fp_mappings")
    .delete()
    .eq("device_id", deviceId)
    .eq("fp_slot", fpSlot);

  if (error) return { error: error.message };
  revalidatePath(`/dashboard/devices/${deviceId}`);
  revalidatePath("/dashboard/people");
  return { ok: true };
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
