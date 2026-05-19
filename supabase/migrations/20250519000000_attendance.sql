-- Fingerprint attendance: people, devices, mappings, events

create type public.device_mode as enum ('idle', 'add', 'scan', 'delete');

create table public.people (
  id uuid primary key default gen_random_uuid(),
  display_name text not null,
  external_id text,
  created_at timestamptz not null default now()
);

create table public.devices (
  id uuid primary key default gen_random_uuid(),
  name text not null,
  api_key_hash text not null unique,
  desired_mode public.device_mode not null default 'idle',
  desired_person_id uuid references public.people (id) on delete set null,
  desired_fp_slot smallint not null default 0,
  command_seq bigint not null default 0,
  ack_seq bigint not null default 0,
  last_seen_at timestamptz,
  created_at timestamptz not null default now(),
  constraint devices_fp_slot_range check (
    desired_fp_slot = 0
    or (desired_fp_slot >= 1 and desired_fp_slot <= 150)
  )
);

create table public.fp_mappings (
  device_id uuid not null references public.devices (id) on delete cascade,
  fp_slot smallint not null,
  person_id uuid not null references public.people (id) on delete cascade,
  enrolled_at timestamptz not null default now(),
  primary key (device_id, fp_slot),
  constraint fp_mappings_slot_range check (fp_slot >= 1 and fp_slot <= 150),
  unique (device_id, person_id)
);

create type public.event_type as enum ('scan', 'enroll');

create table public.attendance_events (
  id uuid primary key default gen_random_uuid(),
  device_id uuid not null references public.devices (id) on delete cascade,
  fp_slot smallint not null,
  person_id uuid references public.people (id) on delete set null,
  event_type public.event_type not null,
  created_at timestamptz not null default now(),
  constraint attendance_fp_slot_range check (fp_slot >= 1 and fp_slot <= 150)
);

create index attendance_events_created_at_idx on public.attendance_events (created_at desc);
create index devices_last_seen_at_idx on public.devices (last_seen_at desc nulls last);

-- Bump command_seq when admin issues a remote command
create or replace function public.issue_device_command(
  p_device_id uuid,
  p_mode public.device_mode,
  p_person_id uuid default null,
  p_fp_slot smallint default 0
)
returns public.devices
language plpgsql
security definer
set search_path = public
as $$
declare
  result public.devices;
begin
  update public.devices
  set
    desired_mode = p_mode,
    desired_person_id = p_person_id,
    desired_fp_slot = coalesce(p_fp_slot, 0),
    command_seq = command_seq + 1
  where id = p_device_id
  returning * into result;
  return result;
end;
$$;

grant execute on function public.issue_device_command(uuid, public.device_mode, uuid, smallint) to authenticated;

-- RLS
alter table public.people enable row level security;
alter table public.devices enable row level security;
alter table public.fp_mappings enable row level security;
alter table public.attendance_events enable row level security;

create policy "authenticated_all_people"
  on public.people for all to authenticated using (true) with check (true);

create policy "authenticated_all_devices"
  on public.devices for all to authenticated using (true) with check (true);

create policy "authenticated_all_fp_mappings"
  on public.fp_mappings for all to authenticated using (true) with check (true);

create policy "authenticated_all_attendance_events"
  on public.attendance_events for all to authenticated using (true) with check (true);

-- Realtime
alter publication supabase_realtime add table public.attendance_events;
alter publication supabase_realtime add table public.devices;
