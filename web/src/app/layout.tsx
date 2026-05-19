import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Fingerprint Attendance",
  description: "Manage scanners, people, and attendance",
};

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
