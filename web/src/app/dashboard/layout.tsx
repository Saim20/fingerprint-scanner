import { Nav } from "@/components/Nav";

export default function DashboardLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <main className="max-w-5xl mx-auto p-6">
      <Nav />
      {children}
    </main>
  );
}
