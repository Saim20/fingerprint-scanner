"use client";

import { importPeopleFromCsv, type CsvPersonRow } from "@/lib/actions/admin";
import { useRouter } from "next/navigation";
import { useMemo, useState } from "react";

type ParsedRow = CsvPersonRow;

function parseCsvLine(line: string): string[] {
  const fields: string[] = [];
  let current = "";
  let inQuotes = false;

  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (inQuotes) {
      if (ch === '"') {
        if (line[i + 1] === '"') {
          current += '"';
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        current += ch;
      }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ",") {
      fields.push(current);
      current = "";
    } else {
      current += ch;
    }
  }
  fields.push(current);
  return fields.map((f) => f.trim());
}

function normalizeHeader(cell: string): string {
  return cell.trim().toLowerCase().replace(/\s+/g, "_");
}

function parseCsv(text: string): { rows: ParsedRow[]; errors: string[] } {
  const lines = text
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter((l) => l.length > 0);

  if (lines.length === 0) {
    return { rows: [], errors: ["File is empty"] };
  }

  const firstFields = parseCsvLine(lines[0]);
  const headerNorm = firstFields.map(normalizeHeader);

  let idIdx = 0;
  let nameIdx = 1;
  let startLine = 0;

  const idAliases = new Set(["id", "external_id", "externalid", "user_id", "userid"]);
  const nameAliases = new Set(["name", "display_name", "displayname", "full_name", "fullname"]);

  const idHeaderIdx = headerNorm.findIndex((h) => idAliases.has(h));
  const nameHeaderIdx = headerNorm.findIndex((h) => nameAliases.has(h));

  if (idHeaderIdx >= 0 && nameHeaderIdx >= 0) {
    idIdx = idHeaderIdx;
    nameIdx = nameHeaderIdx;
    startLine = 1;
  } else if (firstFields.length < 2) {
    return { rows: [], errors: ["Each row needs at least two columns: ID and name"] };
  }

  const rows: ParsedRow[] = [];
  const errors: string[] = [];
  const seenIds = new Set<string>();

  for (let i = startLine; i < lines.length; i++) {
    const fields = parseCsvLine(lines[i]);
    const external_id = (fields[idIdx] ?? "").trim();
    const display_name = (fields[nameIdx] ?? "").trim();
    const lineNum = i + 1;

    if (!external_id) {
      errors.push(`Line ${lineNum}: missing ID`);
      continue;
    }
    if (!display_name) {
      errors.push(`Line ${lineNum}: missing name for ID "${external_id}"`);
      continue;
    }
    if (seenIds.has(external_id)) {
      errors.push(`Line ${lineNum}: duplicate ID "${external_id}" in file`);
      continue;
    }
    seenIds.add(external_id);
    rows.push({ external_id, display_name });
  }

  if (rows.length === 0 && errors.length === 0) {
    errors.push("No valid rows found");
  }

  return { rows, errors };
}

export function CsvPeopleImport() {
  const router = useRouter();
  const [fileName, setFileName] = useState<string | null>(null);
  const [parsed, setParsed] = useState<ParsedRow[]>([]);
  const [parseErrors, setParseErrors] = useState<string[]>([]);
  const [importing, setImporting] = useState(false);
  const [result, setResult] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const preview = useMemo(() => parsed.slice(0, 10), [parsed]);

  async function onFileChange(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    setResult(null);
    setError(null);
    if (!file) {
      setFileName(null);
      setParsed([]);
      setParseErrors([]);
      return;
    }

    const text = await file.text();
    const { rows, errors } = parseCsv(text);
    setFileName(file.name);
    setParsed(rows);
    setParseErrors(errors);
  }

  async function onImport() {
    if (!parsed.length) return;
    setImporting(true);
    setError(null);
    setResult(null);

    const res = await importPeopleFromCsv(parsed);
    setImporting(false);

    if ("error" in res && res.error) {
      setError(res.error);
      return;
    }

    const parts = [
      `${res.created} created`,
      `${res.updated} updated`,
    ];
    if ((res.failed ?? 0) > 0) {
      parts.push(`${res.failed} failed`);
    }
    setResult(parts.join(", "));
    if (res.errors?.length) {
      setError(res.errors.join("; "));
    }
    router.refresh();
  }

  const canImport = parsed.length > 0 && parseErrors.length === 0 && !importing;

  return (
    <div className="card max-w-2xl space-y-4 mb-8">
      <h2 className="font-medium">Import from CSV</h2>
      <p className="text-sm text-[var(--muted)]">
        Two columns: ID and name. Header row optional (<code className="text-xs">id,name</code> or{" "}
        <code className="text-xs">external_id,display_name</code>). IDs are stored as strings
        (e.g. EMP-042, A10).
      </p>

      <input
        type="file"
        accept=".csv,text/csv"
        className="text-sm"
        onChange={onFileChange}
      />

      {fileName && (
        <p className="text-sm text-[var(--muted)]">
          {fileName}: {parsed.length} row{parsed.length === 1 ? "" : "s"}
          {parseErrors.length > 0 && ` · ${parseErrors.length} error(s)`}
        </p>
      )}

      {parseErrors.length > 0 && (
        <ul className="text-sm text-red-400 list-disc pl-5 space-y-1">
          {parseErrors.map((msg) => (
            <li key={msg}>{msg}</li>
          ))}
        </ul>
      )}

      {preview.length > 0 && (
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="text-left text-[var(--muted)] border-b border-[var(--border)]">
                <th className="py-2 pr-4">ID</th>
                <th className="py-2">Name</th>
              </tr>
            </thead>
            <tbody>
              {preview.map((row) => (
                <tr key={row.external_id} className="border-b border-[var(--border)]">
                  <td className="py-2 pr-4 font-mono text-xs">{row.external_id}</td>
                  <td className="py-2">{row.display_name}</td>
                </tr>
              ))}
            </tbody>
          </table>
          {parsed.length > 10 && (
            <p className="text-xs text-[var(--muted)] mt-2">
              Showing 10 of {parsed.length} rows
            </p>
          )}
        </div>
      )}

      {error && <p className="text-red-400 text-sm">{error}</p>}
      {result && <p className="text-amber-300 text-sm">{result}</p>}

      <button
        type="button"
        className="btn btn-primary"
        disabled={!canImport}
        onClick={onImport}
      >
        {importing ? "Importing…" : "Import"}
      </button>
    </div>
  );
}
