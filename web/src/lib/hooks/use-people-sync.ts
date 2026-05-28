"use client";

import { createClient } from "@/lib/supabase/client";
import { sortPeopleByExternalId } from "@/lib/sort-people";
import type { Person } from "@/lib/types";
import { useCallback, useEffect, useState } from "react";

export function usePeopleSync(initialPeople: Person[]) {
  const [people, setPeople] = useState(initialPeople);

  useEffect(() => {
    setPeople(initialPeople);
  }, [initialPeople]);

  const fetchPeople = useCallback(async () => {
    const supabase = createClient();
    const { data } = await supabase.from("people").select("*");
    if (data) setPeople(sortPeopleByExternalId(data as Person[]));
  }, []);

  useEffect(() => {
    const supabase = createClient();

    const channel = supabase
      .channel("people-sync")
      .on(
        "postgres_changes",
        { event: "*", schema: "public", table: "people" },
        () => {
          void fetchPeople();
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, [fetchPeople]);

  return { people, refreshPeople: fetchPeople };
}
