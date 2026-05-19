-- Template sync metadata + unique external IDs for 1:1 person ↔ slot mapping

create unique index if not exists people_external_id_unique
  on public.people (external_id)
  where external_id is not null;

alter table public.devices
  add column if not exists reported_slots_hash text,
  add column if not exists reported_fp_count smallint,
  add column if not exists reported_fp_slots smallint[] not null default '{}',
  add column if not exists last_template_sync_at timestamptz;

-- Assign / update a cloud mapping without remote enroll (device already has template).
create or replace function public.assign_fp_mapping(
  p_device_id uuid,
  p_fp_slot smallint,
  p_person_id uuid
)
returns public.fp_mappings
language plpgsql
security definer
set search_path = public
as $$
declare
  result public.fp_mappings;
begin
  if p_fp_slot < 1 or p_fp_slot > 150 then
    raise exception 'fp_slot out of range';
  end if;

  insert into public.fp_mappings (device_id, fp_slot, person_id, enrolled_at)
  values (p_device_id, p_fp_slot, p_person_id, now())
  on conflict (device_id, fp_slot) do update
    set person_id = excluded.person_id,
        enrolled_at = now()
  returning * into result;

  return result;
end;
$$;

grant execute on function public.assign_fp_mapping(uuid, smallint, uuid) to authenticated;
