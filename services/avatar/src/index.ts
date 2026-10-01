// Unauthenticated badge avatar relay: GET /v1/<network>/<handle> returns a
// 160x160 JPEG the badge can store directly. The unavatar key stays here; the
// badge sends no credentials.
import { parse } from './route';

export interface Env {
  IMAGES: ImagesBinding;
  LOOKUPS: RateLimit;
  UNAVATAR_API_KEY?: string;
}

const SIDE = 160;
const MAX_SOURCE_BYTES = 2 * 1024 * 1024;
const MAX_OUTPUT_BYTES = 64 * 1024; // The badge accepts at most 128 KiB.
const SOURCE_TIMEOUT_MS = 10_000;
const CACHE_SECONDS = 7 * 24 * 60 * 60;

function json(status: number, error: string, message: string): Response {
  return new Response(JSON.stringify({ error, message }), { status, headers: {
    'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  } });
}

async function bounded(body: ReadableStream<Uint8Array> | null, max: number): Promise<Uint8Array | null> {
  if (!body) return null;
  const reader = body.getReader();
  const parts: Uint8Array[] = [];
  let length = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      length += value.byteLength;
      if (length > max) return null;
      parts.push(value);
    }
  } finally { void reader.cancel().catch(() => {}); }
  const joined = new Uint8Array(length);
  let offset = 0;
  for (const part of parts) { joined.set(part, offset); offset += part.byteLength; }
  return joined;
}

function stream(bytes: Uint8Array): ReadableStream<Uint8Array> {
  return new Response(bytes).body!;
}

export default {
  async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
    const url = new URL(request.url);
    if (request.method !== 'GET' || url.search) return json(404, 'not_found', 'Not found.');
    if (url.pathname === '/health') return json(200, 'none', 'ready');
    const target = parse(url.pathname);
    if (!target) return json(404, 'not_found', 'Use /v1/<network>/<handle> for a supported network.');

    const key = new Request(`https://avatar.chan.dev/v1/${target.network}/${target.handle}`);
    const cached = await caches.default.match(key);
    if (cached) return cached;

    if (!env.UNAVATAR_API_KEY) return json(503, 'unconfigured', 'Photo lookup is not configured.');
    // A single shared key: callers share one public IP at the venue.
    if (!(await env.LOOKUPS.limit({ key: 'unavatar' })).success) {
      return json(429, 'rate_limited', 'Too many photo lookups. Try again in a minute.');
    }

    let source: Response;
    try {
      source = await fetch(`https://unavatar.io/${target.network}/${encodeURIComponent(target.handle)}?fallback=false`, {
        headers: { 'x-api-key': env.UNAVATAR_API_KEY, Accept: 'image/*' },
        signal: AbortSignal.timeout(SOURCE_TIMEOUT_MS),
      });
    } catch {
      return json(504, 'source_timeout', 'The photo service did not respond.');
    }
    if (source.status === 404) {
      void source.body?.cancel();
      return json(404, 'no_photo', 'No public photo was found for that account.');
    }
    if (source.status === 401 || source.status === 403) {
      void source.body?.cancel();
      console.error('avatar source rejected the key', { status: source.status });
      return json(503, 'unconfigured', 'Photo lookup is not configured.');
    }
    const type = source.headers.get('Content-Type') ?? '';
    if (!source.ok || !type.startsWith('image/')) {
      void source.body?.cancel();
      console.error('avatar source failure', { network: target.network, status: source.status });
      return json(502, 'source_failed', 'The photo service could not return a photo.');
    }
    const original = await bounded(source.body, MAX_SOURCE_BYTES);
    if (!original) return json(502, 'source_too_large', 'The photo was too large.');

    let jpeg: Uint8Array | null;
    try {
      const output = await env.IMAGES.input(stream(original))
        .transform({ width: SIDE, height: SIDE, fit: 'cover' })
        .output({ format: 'image/jpeg', quality: 85 });
      jpeg = await bounded(output.image(), MAX_OUTPUT_BYTES);
    } catch {
      console.error('avatar transform failure', { network: target.network });
      return json(502, 'convert_failed', 'The photo could not be converted.');
    }
    if (!jpeg || jpeg[0] !== 0xff || jpeg[1] !== 0xd8) return json(502, 'convert_failed', 'The photo could not be converted.');

    const result = new Response(jpeg, { headers: {
      'Content-Type': 'image/jpeg', 'Content-Length': String(jpeg.byteLength),
      'Cache-Control': `public, max-age=${CACHE_SECONDS}`, 'X-Content-Type-Options': 'nosniff',
    } });
    ctx.waitUntil(caches.default.put(key, result.clone()));
    return result;
  },
} satisfies ExportedHandler<Env>;
