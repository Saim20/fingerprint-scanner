-- Web roster + device page need live fp_mappings changes (enroll_done INSERT/upsert).
alter publication supabase_realtime add table public.fp_mappings;
