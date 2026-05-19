"use client";

import { ConfirmButton } from "@/components/ConfirmButton";
import { deletePerson, updatePerson } from "@/lib/actions/admin";
import { useRouter } from "next/navigation";
import { useState } from "react";

export function PersonManagePanel({
  personId,
  displayName,
  externalId,
}: {
  personId: string;
  displayName: string;
  externalId: string | null;
}) {
  const router = useRouter();
  const [name, setName] = useState(displayName);
  const [extId, setExtId] = useState(externalId ?? "");
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function handleSave(e: React.FormEvent) {
    e.preventDefault();
    setSaving(true);
    setError(null);
    const result = await updatePerson(personId, name, extId || null);
    setSaving(false);
    if (result.error) {
      setError(result.error);
      return;
    }
    router.refresh();
  }

  return (
    <div className="card mb-8 space-y-6">
      <h2 className="text-lg font-medium">Edit person</h2>
      <form onSubmit={handleSave} className="space-y-4 max-w-md">
        <div>
          <label className="label" htmlFor="edit-name">
            Display name
          </label>
          <input
            id="edit-name"
            className="input"
            value={name}
            onChange={(e) => setName(e.target.value)}
            required
          />
        </div>
        <div>
          <label className="label" htmlFor="edit-ext">
            External ID
          </label>
          <input
            id="edit-ext"
            className="input"
            value={extId}
            onChange={(e) => setExtId(e.target.value)}
          />
        </div>
        <button type="submit" className="btn btn-primary" disabled={saving}>
          {saving ? "Saving…" : "Save changes"}
        </button>
      </form>

      {error && <p className="text-red-400 text-sm">{error}</p>}

      <div className="pt-4 border-t border-[var(--border)]">
        <p className="text-sm text-[var(--muted)] mb-3">
          Deletes this person and all fingerprint mappings linked to them.
        </p>
        <ConfirmButton
          label="Delete person"
          confirmLabel="Yes, delete person"
          pendingLabel="Deleting…"
          onConfirm={async () => {
            const result = await deletePerson(personId);
            if (result && "error" in result && result.error) {
              setError(result.error);
            }
          }}
        />
      </div>
    </div>
  );
}
