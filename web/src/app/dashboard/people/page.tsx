import { createClient } from "@/lib/supabase/server";
import type { Person } from "@/lib/types";
import Link from "next/link";
import { revalidatePath } from "next/cache";

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
  const { data: people } = await supabase
    .from("people")
    .select("*")
    .order("display_name");

  return (
    <div>
      <h1 className="text-2xl font-semibold mb-6">People</h1>

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
            External ID (optional)
          </label>
          <input id="external_id" name="external_id" className="input" />
        </div>
        <button type="submit" className="btn btn-primary">
          Add
        </button>
      </form>

      <ul className="space-y-2">
        {(people as Person[] | null)?.map((p) => (
          <li key={p.id}>
            <Link
              href={`/dashboard/people/${p.id}`}
              className="card block no-underline hover:border-[var(--accent)]"
            >
              <span className="font-medium text-[var(--text)]">{p.display_name}</span>
              {p.external_id && (
                <span className="text-sm text-[var(--muted)] ml-2">
                  ({p.external_id})
                </span>
              )}
            </Link>
          </li>
        ))}
      </ul>
    </div>
  );
}
