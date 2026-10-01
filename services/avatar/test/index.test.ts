// Offline checks: unavatar, Cloudflare Images and the edge cache are fakes.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import worker, { type Env } from '../src/index';
import { readFileSync } from 'node:fs';
import { HANDLES, parse } from '../src/route';

const JPEG = new Uint8Array([0xff, 0xd8, 0xff, 0xe0, 1, 2, 3, 0xff, 0xd9]);
const store = new Map<string, Response>();
let transforms: unknown[] = [];
let limited = false;

function env(overrides: Partial<Env> = {}): Env {
  return {
    UNAVATAR_API_KEY: 'sk_test',
    LOOKUPS: { limit: async () => ({ success: !limited }) } as unknown as RateLimit,
    IMAGES: {
      input: () => ({
        transform(options: unknown) { transforms.push(options); return this; },
        output: async () => ({ image: () => new Response(JPEG).body!, response: () => new Response(JPEG), contentType: () => 'image/jpeg' }),
      }),
    } as unknown as ImagesBinding,
    ...overrides,
  };
}
const ctx = { waitUntil: (promise: Promise<unknown>) => { void promise; }, passThroughOnException() {} } as unknown as ExecutionContext;
const get = (path: string, e = env()) => worker.fetch(new Request(`https://avatar.chan.dev${path}`), e, ctx);

beforeEach(() => {
  store.clear(); transforms = []; limited = false;
  vi.stubGlobal('caches', { default: {
    match: async (request: Request) => store.get(request.url)?.clone(),
    put: async (request: Request, response: Response) => { store.set(request.url, response); },
  } });
});
afterEach(() => { vi.unstubAllGlobals(); });

describe('parse', () => {
  it('accepts badge networks and normalizes handles', () => {
    expect(parse('/v1/github/Chantastic')).toEqual({ network: 'github', handle: 'chantastic' });
    expect(parse('/v1/x/%40chantastic')).toEqual({ network: 'x', handle: 'chantastic' });
    expect(parse('/v1/linkedin/michael-chan-1234')).toEqual({ network: 'linkedin', handle: 'michael-chan-1234' });
  });
  it('rejects other networks, paths and malformed handles', () => {
    for (const path of ['/v1/instagram/a', '/v1/myspace/tom', '/v1/github/a/b', '/v1/github/-bad', '/v1/github/a--b',
      '/v1/x/way_too_long_handle', '/v1/github/%2e%2e', '/v1/github/%E0%A4%A', '/v2/github/a',
      '/v1/github/https%3A%2F%2Fevil.example']) {
      expect(parse(path), path).toBeNull();
    }
  });
});

describe('networks', () => {
  it('match the badge firmware table exactly', () => {
    const header = readFileSync(new URL('../../../firmware/factory_badge/main/social_networks.h', import.meta.url), 'utf8');
    const keys = [...header.matchAll(/\{"([a-z]+)", "[^"]+", "https:\/\//g)].map(m => m[1]);
    expect(keys).toHaveLength(10);
    expect(Object.keys(HANDLES)).toEqual(keys);
  });
  it('accept each network\'s badge-normalized handles', () => {
    const ok: [string, string][] = [['bluesky', 'chan.dev'], ['bluesky', 'chantastic.bsky.social'], ['huggingface', 'julien-c'],
      ['youtube', 'GitHub'], ['gitlab', 'sytses'], ['substack', 'bankless'], ['dribbble', 'omidnikrah'], ['threads', 'zuck'],
      ['linkedin', 'michael-chan-1234'], ['linkedin', 'some_one']];
    for (const [network, handle] of ok) expect(parse(`/v1/${network}/${handle}`), `${network}/${handle}`).not.toBeNull();
    for (const path of ['/v1/bluesky/nodot', '/v1/bluesky/bad..dev', '/v1/substack/a.b', '/v1/youtube/ab', '/v1/threads/bad-name', '/v1/dribbble/x'])
      expect(parse(path), path).toBeNull();
  });
});

describe('fetch', () => {
  it('converts an unavatar photo to a cached 160px JPEG with the secret key', async () => {
    const calls: Request[] = [];
    vi.stubGlobal('fetch', async (input: string, init: RequestInit) => {
      calls.push(new Request(input, init));
      return new Response(new Uint8Array([0x89, 0x50]), { headers: { 'Content-Type': 'image/png' } });
    });
    const first = await get('/v1/github/Chantastic');
    expect(first.status).toBe(200);
    expect(first.headers.get('Content-Type')).toBe('image/jpeg');
    expect(new Uint8Array(await first.arrayBuffer())).toEqual(JPEG);
    expect(transforms).toEqual([{ width: 160, height: 160, fit: 'cover' }]);
    expect(calls).toHaveLength(1);
    expect(calls[0].url).toBe('https://unavatar.io/github/chantastic?fallback=false');
    expect(calls[0].headers.get('x-api-key')).toBe('sk_test');

    const second = await get('/v1/github/chantastic');
    expect(second.status).toBe(200);
    expect(calls).toHaveLength(1); // Served from the edge cache.
  });

  it('reports missing photos and source failures without caching them', async () => {
    vi.stubGlobal('fetch', async () => new Response('{}', { status: 404 }));
    expect((await get('/v1/x/nobody')).status).toBe(404);
    vi.stubGlobal('fetch', async () => new Response('<html>', { headers: { 'Content-Type': 'text/html' } }));
    expect((await get('/v1/x/nobody')).status).toBe(502);
    vi.stubGlobal('fetch', async () => new Response('{"code":"EAPIKEY"}', { status: 401 }));
    expect((await get('/v1/x/nobody')).status).toBe(503); // A rejected key reads as misconfiguration.
    vi.stubGlobal('fetch', async () => { throw new DOMException('timeout', 'TimeoutError'); });
    expect((await get('/v1/x/nobody')).status).toBe(504);
    expect(store.size).toBe(0);
  });

  it('rejects oversized sources and non-JPEG conversions', async () => {
    vi.stubGlobal('fetch', async () => new Response(new Uint8Array(2 * 1024 * 1024 + 1), { headers: { 'Content-Type': 'image/png' } }));
    expect((await get('/v1/github/big')).status).toBe(502);
    vi.stubGlobal('fetch', async () => new Response(new Uint8Array([1]), { headers: { 'Content-Type': 'image/png' } }));
    const broken = env({ IMAGES: { input: () => ({ transform() { return this; },
      output: async () => ({ image: () => new Response(new Uint8Array([0x89, 0x50])).body! }) }) } as unknown as ImagesBinding });
    expect((await get('/v1/github/odd', broken)).status).toBe(502);
  });

  it('enforces the shared lookup limit and configuration before calling unavatar', async () => {
    const fetcher = vi.fn();
    vi.stubGlobal('fetch', fetcher);
    limited = true;
    expect((await get('/v1/github/someone')).status).toBe(429);
    limited = false;
    expect((await get('/v1/github/someone', env({ UNAVATAR_API_KEY: undefined }))).status).toBe(503);
    expect(fetcher).not.toHaveBeenCalled();
  });

  it('refuses other methods, query strings and unknown routes', async () => {
    const fetcher = vi.fn();
    vi.stubGlobal('fetch', fetcher);
    expect((await worker.fetch(new Request('https://avatar.chan.dev/v1/github/a', { method: 'POST' }), env(), ctx)).status).toBe(404);
    expect((await get('/v1/github/a?url=https://evil.example')).status).toBe(404);
    expect((await get('/')).status).toBe(404);
    expect((await get('/health')).status).toBe(200);
    expect(fetcher).not.toHaveBeenCalled();
  });
});
