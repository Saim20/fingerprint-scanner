"use client";

import { ConfirmButton } from "@/components/ConfirmButton";
import { deleteMapping } from "@/lib/actions/admin";
import { useRouter } from "next/navigation";
import { useState } from "react";

type MappingRow = {
  fp_slot: number;
  person_id: string;
  enrolled_at: string;
  personLabel: string;
};

export function MappingTable({
  deviceId,
  mappings,
}: {
  deviceId: string;
  mappings: MappingRow[];
}) {
  const router = useRouter();
  const [error, setError] = useState<string | null>(null);

  if (!mappings.length) {
    return (
      <p className="text-[var(--muted)] text-sm">No enrollments on this device yet.</p>
    );
  }

  return (
    <div>
      {error && <p className="text-red-400 text-sm mb-2">{error}</p>}
      <p className="text-xs text-[var(--muted)] mb-3">
        Removing a mapping only updates the cloud record. Use remote delete to
        erase the template on the scanner.
      </p>
      <table className="w-full text-sm">
        <thead>
          <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
            <th className="py-2">Slot</th>
            <th className="py-2">Person</th>
            <th className="py-2">Enrolled</th>
            <th className="py-2 w-32"></th>
          </tr>
        </thead>
        <tbody>
          {mappings.map((m) => (
            <tr key={m.fp_slot} className="border-b border-[var(--border)]">
              <td className="py-2">{m.fp_slot}</td>
              <td className="py-2">{m.personLabel}</td>
              <td className="py-2 text-[var(--muted)]">
                {new Date(m.enrolled_at).toLocaleString()}
              </td>
              <td className="py-2 text-right">
                <ConfirmButton
                  label="Unmap"
                  confirmLabel="Remove"
                  variant="ghost"
                  onConfirm={async () => {
                    setError(null);
                    const result = await deleteMapping(deviceId, m.fp_slot);
                    if (result.error) {
                      setError(result.error);
                      return;
                    }
                    router.refresh();
                  }}
                />
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
