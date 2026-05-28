import type { Person } from "@/lib/types";

export function externalIdKey(external_id: string | null | undefined): string | null {
  const trimmed = external_id?.trim();
  return trimmed ? trimmed : null;
}

/** String sort on external_id; empty IDs last; then display_name. */
export function comparePeopleByExternalId(a: Person, b: Person): number {
  const idA = externalIdKey(a.external_id);
  const idB = externalIdKey(b.external_id);

  if (!idA && !idB) {
    return a.display_name.localeCompare(b.display_name, undefined, { sensitivity: "base" });
  }
  if (!idA) return 1;
  if (!idB) return -1;

  const cmp = idA.localeCompare(idB, undefined, { sensitivity: "base", numeric: true });
  if (cmp !== 0) return cmp;
  return a.display_name.localeCompare(b.display_name, undefined, { sensitivity: "base" });
}

export function sortPeopleByExternalId(people: Person[]): Person[] {
  return [...people].sort(comparePeopleByExternalId);
}
