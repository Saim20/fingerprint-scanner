import { createClient } from "@/lib/supabase/server";
import Link from "next/link";
import { PersonManagePanel } from "../PersonManagePanel";
import { PersonEnrollments } from "../PersonEnrollments";

export default async function PersonDetailPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const { id } = await params;
  const supabase = await createClient();

  const { data: person } = await supabase
    .from("people")
    .select("*")
    .eq("id", id)
    .single();

  if (!person) {
    return <p>Person not found.</p>;
  }

  return (
    <div>
      <Link href="/dashboard/people" className="text-sm text-[var(--muted)]">
        ← People
      </Link>
      <h1 className="text-2xl font-semibold mt-2 mb-6">{person.display_name}</h1>

      <PersonManagePanel
        personId={person.id}
        displayName={person.display_name}
        externalId={person.external_id}
      />

      <h2 className="text-lg font-medium mb-3">Enrolled on devices</h2>
      <PersonEnrollments personId={person.id} />
    </div>
  );
}
