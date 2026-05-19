-- Passive clock-in: when false, device only runs explicit dashboard commands (no always-on scan).
alter table public.devices
  add column if not exists background_scan boolean not null default false;

comment on column public.devices.background_scan is
  'When true, device passively scans for fingerprints and logs attendance. When false, only remote commands / GO.';
