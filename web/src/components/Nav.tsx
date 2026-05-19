import Link from "next/link";

const links = [
  { href: "/dashboard", label: "Devices" },
  { href: "/dashboard/people", label: "People" },
  { href: "/dashboard/attendance", label: "Attendance" },
];

export function Nav() {
  return (
    <nav className="flex flex-wrap items-center gap-4 border-b border-[var(--border)] pb-4 mb-8">
      <Link
        href="/dashboard"
        className="text-lg font-semibold text-[var(--text)] no-underline"
      >
        Fingerprint Admin
      </Link>
      <div className="flex gap-4 ml-auto">
        {links.map((l) => (
          <Link
            key={l.href}
            href={l.href}
            className="text-sm text-[var(--muted)] hover:text-[var(--text)]"
          >
            {l.label}
          </Link>
        ))}
      </div>
      <form action="/auth/signout" method="post">
        <button type="submit" className="btn btn-ghost text-sm">
          Sign out
        </button>
      </form>
    </nav>
  );
}
