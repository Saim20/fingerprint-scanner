import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Avoid webpack vendor-chunk resolution errors for Supabase in dev/server bundles.
  serverExternalPackages: ["@supabase/supabase-js", "@supabase/ssr"],
};

export default nextConfig;
