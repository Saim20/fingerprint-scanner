import { createClient } from "@/lib/supabase/server";
import { generateDeviceApiKey, sha256Hex } from "@/lib/hash";
import { redirect } from "next/navigation";

async function createDevice(formData: FormData) {
  "use server";
  const name = (formData.get("name") as string)?.trim();
  if (!name) return;

  const apiKey = generateDeviceApiKey();
  const api_key_hash = await sha256Hex(apiKey);

  const supabase = await createClient();
  const { data, error } = await supabase
    .from("devices")
    .insert({ name, api_key_hash })
    .select("id")
    .single();

  if (error || !data) {
    throw new Error(error?.message ?? "Failed to create device");
  }

  redirect(
    `/dashboard/devices/${data.id}?api_key=${encodeURIComponent(apiKey)}`,
  );
}

export default function NewDevicePage() {
  return (
    <div>
      <h1 className="text-2xl font-semibold mb-6">Add device</h1>
      <form action={createDevice} className="card max-w-md space-y-4">
        <div>
          <label className="label" htmlFor="name">
            Device name
          </label>
          <input
            id="name"
            name="name"
            className="input"
            placeholder="Front door scanner"
            required
          />
        </div>
        <button type="submit" className="btn btn-primary">
          Create
        </button>
      </form>
    </div>
  );
}
