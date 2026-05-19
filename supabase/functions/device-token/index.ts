import { createClient } from "https://esm.sh/@supabase/supabase-js@2.49.1";
import { create, getNumericDate } from "https://deno.land/x/djwt@v3.0.2/mod.ts";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type",
};

async function sha256Hex(input: string): Promise<string> {
  const data = new TextEncoder().encode(input);
  const hash = await crypto.subtle.digest("SHA-256", data);
  return Array.from(new Uint8Array(hash))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders, "Content-Type": "application/json" },
  });
}

function getBearer(req: Request): string | null {
  const auth = req.headers.get("Authorization");
  if (!auth?.startsWith("Bearer ")) return null;
  return auth.slice(7).trim();
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse({ error: "method not allowed" }, 405);
  }

  const apiKey = getBearer(req);
  if (!apiKey) {
    return jsonResponse({ error: "missing bearer token" }, 401);
  }

  const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
  const serviceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
  const jwtSecret = Deno.env.get("SUPABASE_JWT_SECRET") ?? Deno.env.get("JWT_SECRET");
  if (!jwtSecret) {
    return jsonResponse({ error: "JWT secret not configured" }, 500);
  }

  const supabase = createClient(supabaseUrl, serviceKey);
  const hash = await sha256Hex(apiKey);
  const { data: device, error } = await supabase
    .from("devices")
    .select("id")
    .eq("api_key_hash", hash)
    .maybeSingle();

  if (error || !device) {
    return jsonResponse({ error: "invalid device key" }, 401);
  }

  const key = await crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(jwtSecret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );

  const expiresIn = 3600;
  const accessToken = await create(
    { alg: "HS256", typ: "JWT" },
    {
      aud: "authenticated",
      exp: getNumericDate(expiresIn),
      iat: getNumericDate(0),
      iss: `${supabaseUrl}/auth/v1`,
      sub: device.id,
      role: "authenticated",
      app_metadata: { device_id: device.id },
    },
    key,
  );

  return jsonResponse({
    access_token: accessToken,
    expires_in: expiresIn,
    device_id: device.id,
  });
});
