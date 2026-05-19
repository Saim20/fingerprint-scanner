-- Remote wipe all templates on device
alter type public.device_mode add value if not exists 'clear';
