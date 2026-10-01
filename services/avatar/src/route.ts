// Keys match the badge's photo Providers (firmware/factory_badge/main/
// social_networks.h): its five named networks plus providers recognized in
// Other links. They are also unavatar's provider paths. Only these networks and
// well-formed handles are accepted, so the relay is never a general fetch
// proxy. Handles are compared lowercase.
const label = '[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?';
export const HANDLES: Record<string, RegExp> = {
  linkedin: /^[a-z0-9_-]{1,100}$/,
  x: /^[a-z0-9_]{1,15}$/,
  github: /^[a-z0-9](?:[a-z0-9]|-(?=[a-z0-9])){0,38}$/,
  huggingface: /^[a-z0-9][a-z0-9._-]{0,95}$/,
  youtube: /^[a-z0-9._-]{3,30}$/,
  bluesky: new RegExp(`^(?=.{1,253}$)(?:${label}\\.)+${label}$`),
  gitlab: /^[a-z0-9][a-z0-9._-]{1,63}$/,
  substack: new RegExp(`^${label}$`),
  dribbble: /^[a-z0-9][a-z0-9_-]{1,31}$/,
  threads: /^[a-z0-9._]{1,30}$/,
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
