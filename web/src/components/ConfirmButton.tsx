"use client";

import { useState } from "react";

export function ConfirmButton({
  label,
  confirmLabel,
  pendingLabel = "Working…",
  variant = "danger",
  onConfirm,
  disabled,
}: {
  label: string;
  confirmLabel: string;
  pendingLabel?: string;
  variant?: "danger" | "ghost";
  onConfirm: () => Promise<void>;
  disabled?: boolean;
}) {
  const [armed, setArmed] = useState(false);
  const [loading, setLoading] = useState(false);

  async function handleClick() {
    if (!armed) {
      setArmed(true);
      return;
    }
    setLoading(true);
    try {
      await onConfirm();
    } finally {
      setLoading(false);
      setArmed(false);
    }
  }

  const className =
    variant === "danger"
      ? `btn btn-danger${armed ? " ring-2 ring-red-300" : ""}`
      : `btn btn-ghost${armed ? " border-[var(--warn)]" : ""}`;

  return (
    <div className="flex flex-wrap items-center gap-2">
      <button
        type="button"
        className={className}
        disabled={disabled || loading}
        onClick={handleClick}
        onBlur={() => !loading && setArmed(false)}
      >
        {loading ? pendingLabel : armed ? confirmLabel : label}
      </button>
      {armed && !loading && (
        <button
          type="button"
          className="btn btn-ghost text-sm"
          onClick={() => setArmed(false)}
        >
          Cancel
        </button>
      )}
    </div>
  );
}
