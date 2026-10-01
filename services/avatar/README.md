# avatar.chan.dev

Temporary (October 2026) relay that gives the conference badge a profile photo
with one unauthenticated request:

```
GET https://avatar.chan.dev/v1/<network>/<handle>  ->  160x160 image/jpeg
```

The Worker (`chan-avatar`) calls [unavatar.io](https://unavatar.io) with the
secret `UNAVATAR_API_KEY`, converts the result with the Cloudflare Images
binding, and caches it at the edge for 7 days. The badge never holds a key.

It is deliberately isolated from `chantastic/chan-services`: no service
bindings, no shared secrets, no account data, its own lockfile, manual deploys.
Delete the Worker and the `avatar.chan.dev` custom domain when the event ends.

## Limits

- Only the badge's ten networks (`github`, `x`, `linkedin`, `bluesky`,
  `huggingface`, `youtube`, `gitlab`, `substack`, `dribbble`, `threads`), with
  handles matching each network's format; no caller-supplied URLs or query
  strings. A test keeps `src/route.ts` in step with the firmware's
  `social_networks.h`.
- Sources over 2 MiB and outputs over 64 KiB are refused (badge maximum: 128 KiB).
- Uncached lookups share one rate limit (30/minute) because attendees share the
  venue's public IP. Cached hits are unlimited.
- Invocation logs are off; errors log the network and status, never the handle.
- X, LinkedIn and Threads photos come from unavatar's unofficial sources and may
  stop working; the badge falls back to manual upload.

## Commands

Run from this directory with Node 24:

```sh
npm ci
npm run check && npm test        # offline; unavatar, Images and cache are fakes
npx wrangler secret put UNAVATAR_API_KEY   # owner only; never commit the key
npm run deploy                   # needs explicit approval
```

`wrangler dev` runs the real Images conversion locally; an invalid key falls back
to unavatar's free tier only for photos it already has cached.
