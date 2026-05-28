export type MappingRow = {
  fp_slot: number;
  person_id: string;
  enrolled_at: string;
  personLabel: string;
  externalId: string | null;
};

function personFields(
  people:
    | { display_name: string; external_id: string | null }
    | { display_name: string; external_id: string | null }[]
    | null,
): { display_name: string; external_id: string | null } | null {
  if (!people) return null;
  if (Array.isArray(people)) return people[0] ?? null;
  return people;
}

export function toMappingRows(
  rows:
    | {
        fp_slot: number;
        person_id: string;
        enrolled_at: string;
        people:
          | { display_name: string; external_id: string | null }
          | { display_name: string; external_id: string | null }[]
          | null;
      }[]
    | null,
): MappingRow[] {
  return (rows ?? []).map((m) => {
    const p = personFields(m.people);
    return {
      fp_slot: m.fp_slot,
      person_id: m.person_id,
      enrolled_at: m.enrolled_at,
      personLabel: p?.display_name ?? m.person_id,
      externalId: p?.external_id ?? null,
    };
  });
}
