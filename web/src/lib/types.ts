export type DeviceMode = "idle" | "add" | "scan" | "delete";

export type Person = {
  id: string;
  display_name: string;
  external_id: string | null;
  created_at: string;
};

export type Device = {
  id: string;
  name: string;
  desired_mode: DeviceMode;
  desired_person_id: string | null;
  desired_fp_slot: number;
  command_seq: number;
  ack_seq: number;
  last_seen_at: string | null;
  created_at: string;
};

export type FpMapping = {
  device_id: string;
  fp_slot: number;
  person_id: string;
  enrolled_at: string;
  devices?: { name: string };
  people?: { display_name: string };
};

export type AttendanceEvent = {
  id: string;
  device_id: string;
  fp_slot: number;
  person_id: string | null;
  event_type: "scan" | "enroll";
  created_at: string;
  devices?: { name: string };
  people?: { display_name: string } | null;
};
