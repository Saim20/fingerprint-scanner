"use client";

import { useDevicesSync } from "@/lib/hooks/use-devices-sync";
import { usePeopleSync } from "@/lib/hooks/use-people-sync";
import type { Device, Person } from "@/lib/types";
import { CsvPeopleImport } from "./CsvPeopleImport";
import { EnrollmentRoster } from "./EnrollmentRoster";
import { PeopleList } from "./PeopleList";

export function PeoplePageClient({
  initialPeople,
  initialDevices,
  addPersonForm,
}: {
  initialPeople: Person[];
  initialDevices: Device[];
  addPersonForm: React.ReactNode;
}) {
  const { people } = usePeopleSync(initialPeople);
  const { devices } = useDevicesSync(initialDevices);
  const deviceOptions = devices.map((d) => ({ id: d.id, name: d.name }));

  return (
    <>
      <CsvPeopleImport />

      <EnrollmentRoster people={people} devices={deviceOptions} />

      {addPersonForm}

      <PeopleList people={people} />
    </>
  );
}
