import { createClient } from "@/lib/supabase/server";
import { sortPeopleByExternalId } from "@/lib/sort-people";
import type { Device, Person } from "@/lib/types";
import { revalidatePath } from "next/cache";
import { PeoplePageClient } from "./PeoplePageClient";

async function createPerson(formData: FormData) {
  "use server";
  const display_name = (formData.get("display_name") as string)?.trim();
  const external_id = (formData.get("external_id") as string)?.trim() || null;
  if (!display_name) return;

  const supabase = await createClient();
  await supabase.from("people").insert({ display_name, external_id });
  revalidatePath("/dashboard/people");
}

export default async function PeoplePage() {
  const supabase = await createClient();
  const [{ data: people }, { data: devices }] = await Promise.all([
    supabase.from("people").select("*"),
    supabase.from("devices").select("id, name").order("name"),
  ]);

  const sortedPeople = sortPeopleByExternalId((people ?? []) as Person[]);

  const addPersonForm = (
    <form action={createPerson} className="card max-w-md space-y-4 mb-8">
      <h2 className="font-medium">Add person</h2>
      <div>
        <label className="label" htmlFor="display_name">
          Display name
        </label>
        <input id="display_name" name="display_name" className="input" required />
      </div>
      <div>
        <label className="label" htmlFor="external_id">
          External ID (optional string — for CSV import and roster sorting)
        </label>
        <input id="external_id" name="external_id" className="input" />
      </div>
      <button type="submit" className="btn btn-primary">
        Add
      </button>
    </form>
  );

  return (
    <div>
      <h1 className="text-2xl font-semibold mb-6">People</h1>

      <PeoplePageClient
        initialPeople={sortedPeople}
        initialDevices={(devices ?? []) as Device[]}
        addPersonForm={addPersonForm}
      />
    </div>
  );
}
