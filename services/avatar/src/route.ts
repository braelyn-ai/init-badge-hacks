// Network names match the badge's social slots and unavatar's provider paths.
// Only these networks and well-formed handles are accepted, so the relay is
// never a general fetch proxy.
const HANDLES: Record<string, RegExp> = {
  github: /^[a-z0-9](?:[a-z0-9]|-(?=[a-z0-9])){0,38}$/,
  x: /^[a-z0-9_]{1,15}$/,
  linkedin: /^[a-z0-9][a-z0-9-]{2,99}$/,
};

export function parse(pathname: string): { network: string; handle: string } | null {
  const match = /^\/v1\/([a-z]+)\/([^/]+)$/.exec(pathname);
  if (!match) return null;
  const network = match[1];
  const pattern = HANDLES[network];
  if (!pattern) return null;
  let handle: string;
  try { handle = decodeURIComponent(match[2]).replace(/^@/, '').toLowerCase(); } catch { return null; }
  return pattern.test(handle) ? { network, handle } : null;
}

