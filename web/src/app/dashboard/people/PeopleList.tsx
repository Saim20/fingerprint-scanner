"use client";

import { ConfirmButton } from "@/components/ConfirmButton";
import { deletePerson } from "@/lib/actions/admin";
import type { Person } from "@/lib/types";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { useState } from "react";

export function PeopleList({ people }: { people: Person[] }) {
  const router = useRouter();
  const [error, setError] = useState<string | null>(null);

  if (!people.length) {
    return <p className="text-[var(--muted)] text-sm">No people yet.</p>;
  }

  return (
    <div>
      {error && <p className="text-red-400 text-sm mb-4">{error}</p>}
      <table className="w-full text-sm">
        <thead>
          <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
            <th className="py-3">Name</th>
            <th className="py-3">External ID</th>
            <th className="py-3 text-right">Actions</th>
          </tr>
        </thead>
        <tbody>
          {people.map((p) => (
            <tr key={p.id} className="border-b border-[var(--border)]">
              <td className="py-3">
                <Link
                  href={`/dashboard/people/${p.id}`}
                  className="font-medium text-[var(--text)] no-underline hover:text-[var(--accent)]"
                >
                  {p.display_name}
                </Link>
              </td>
              <td className="py-3 text-[var(--muted)]">{p.external_id ?? "—"}</td>
              <td className="py-3 text-right">
                <div className="flex justify-end gap-2">
                  <Link
                    href={`/dashboard/people/${p.id}`}
                    className="btn btn-ghost text-xs no-underline"
                  >
                    Edit
                  </Link>
                  <ConfirmButton
                    label="Delete"
                    confirmLabel="Delete"
                    variant="ghost"
                    onConfirm={async () => {
                      setError(null);
                      const result = await deletePerson(p.id);
                      if (result && "error" in result && result.error) {
                        setError(result.error);
                        return;
                      }
                      router.refresh();
                    }}
                  />
                </div>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
