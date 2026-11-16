/**
 * Weekly adult-domain list refresh → exact Set (no bloom false locks).
 * Sources: StevenBlack porn hosts + HaGeZi NSFW (best-effort).
 */
import { GATE_V2 } from "./gate_config.js";
import { setBlocklistHosts, blocklistSize } from "./host_intel.js";

const SOURCES = [
  {
    name: "stevenblack-porn",
    url: "https://raw.githubusercontent.com/StevenBlack/hosts/master/alternates/porn-only/hosts",
  },
  {
    name: "hagezi-nsfw",
    url: "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/adblock/nsfw.txt",
  },
];

function parseHostsFile(text) {
  const out = [];
  const lines = String(text || "").split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    let line = lines[i].trim();
    if (!line || line[0] === "#") continue;
    // hosts: 0.0.0.0 domain
    let m = line.match(/^(?:0\.0\.0\.0|127\.0\.0\.1)\s+(\S+)/i);
    if (m) {
      out.push(m[1].toLowerCase().replace(/^www\./, ""));
      continue;
    }
    // adblock: ||domain^
    m = line.match(/^\|\|([a-z0-9.-]+)\^/i);
    if (m) out.push(m[1].toLowerCase().replace(/^www\./, ""));
  }
  return out;
}

export async function loadCachedBlocklist(storage) {
  const key = GATE_V2.blocklist.storageKey;
  try {
    const r = await storage.local.get(key);
    const pack = r[key];
    if (pack && Array.isArray(pack.hosts) && pack.hosts.length) {
      setBlocklistHosts(pack.hosts);
      return { ok: true, size: pack.hosts.length, at: pack.at || 0, hash: pack.hash || "" };
    }
  } catch (e) {
    /* ignore */
  }
  return { ok: false, size: 0 };
}

function simpleHash(s) {
  let h = 0;
  for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0;
  return String(h);
}

export async function refreshBlocklist(storage, fetchFn) {
  const fetchImpl = fetchFn || fetch;
  const merged = new Set();
  const errors = [];
  for (let i = 0; i < SOURCES.length; i++) {
    try {
      const res = await fetchImpl(SOURCES[i].url, { signal: AbortSignal.timeout(45000) });
      if (!res.ok) {
        errors.push(SOURCES[i].name + ":HTTP" + res.status);
        continue;
      }
      const text = await res.text();
      if (!text || text.length < 200) {
        errors.push(SOURCES[i].name + ":too_small");
        continue;
      }
      if (text.length > 80 * 1024 * 1024) {
        errors.push(SOURCES[i].name + ":too_large");
        continue;
      }
      const hosts = parseHostsFile(text);
      for (let j = 0; j < hosts.length; j++) merged.add(hosts[j]);
    } catch (e) {
      errors.push(SOURCES[i].name + ":" + (e && e.message ? e.message : "fail"));
    }
  }

  if (merged.size < 100) {
    // Keep last-good
    const prev = await loadCachedBlocklist(storage);
    return { ok: false, size: prev.size, errors, keptLastGood: true };
  }

  let list = Array.from(merged);
  if (list.length > GATE_V2.blocklist.maxHosts) list = list.slice(0, GATE_V2.blocklist.maxHosts);
  const hash = simpleHash(list.length + ":" + list[0] + ":" + list[list.length - 1]);
  const pack = { hosts: list, at: Date.now(), hash, errors };
  await storage.local.set({ [GATE_V2.blocklist.storageKey]: pack });
  setBlocklistHosts(list);
  return { ok: true, size: list.length, hash, errors };
}

export function currentBlocklistSize() {
  return blocklistSize();
}
