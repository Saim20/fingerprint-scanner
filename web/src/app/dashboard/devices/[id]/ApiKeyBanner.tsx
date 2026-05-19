"use client";

import { useState } from "react";

export function ApiKeyBanner({ apiKey }: { apiKey: string }) {
  const [copied, setCopied] = useState(false);

  async function copy() {
    await navigator.clipboard.writeText(apiKey);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  }

  return (
    <div className="card mb-6 border-[var(--warn)] bg-[rgba(245,158,11,0.08)]">
      <p className="font-medium text-[var(--warn)] mb-2">
        Save this API key — it is shown only once
      </p>
      <code className="block text-xs break-all p-2 bg-[var(--bg)] rounded mb-3">
        {apiKey}
      </code>
      <p className="text-sm text-[var(--muted)] mb-3">
        On the ESP32 serial monitor: <code>provision {apiKey}</code> then reboot.
      </p>
      <button type="button" onClick={copy} className="btn btn-primary">
        {copied ? "Copied" : "Copy key"}
      </button>
    </div>
  );
}
