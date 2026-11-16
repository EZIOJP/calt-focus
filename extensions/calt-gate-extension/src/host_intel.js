/**
 * Host tiers from host_seed (+ optional weekly blocklist Set).
 */
import seed from "../data/host_seed.json";

function normHost(h) {
  return String(h || "")
    .toLowerCase()
    .replace(/^www\./, "")
    .replace(/:\d+$/, "");
}

function listHas(host, list) {
  const h = normHost(host);
  if (!h || !list || !list.length) return false;
  for (let i = 0; i < list.length; i++) {
    const d = normHost(list[i]);
    if (!d) continue;
    if (h === d || h.endsWith("." + d)) return true;
  }
  return false;
}

function parentHosts(host) {
  const h = normHost(host);
  const parts = h.split(".").filter(Boolean);
  const out = [h];
  for (let i = 1; i < parts.length - 1; i++) {
    out.push(parts.slice(i).join("."));
  }
  return out;
}

let blocklistSet = null;

export function setBlocklistHosts(hosts) {
  blocklistSet = new Set();
  if (!hosts || !hosts.length) return;
  for (let i = 0; i < hosts.length; i++) {
    const d = normHost(hosts[i]);
    if (d && d.indexOf(".") >= 0) blocklistSet.add(d);
  }
}

export function blocklistSize() {
  return blocklistSet ? blocklistSet.size : 0;
}

export function inBlocklist(host) {
  if (!blocklistSet || !blocklistSet.size) return false;
  const parents = parentHosts(host);
  for (let i = 0; i < parents.length; i++) {
    if (blocklistSet.has(parents[i])) return true;
  }
  return false;
}

export function suffixPorn(host) {
  const h = normHost(host);
  const suffixes = seed.suffixes || [];
  for (let i = 0; i < suffixes.length; i++) {
    if (h.endsWith(suffixes[i])) return true;
  }
  return false;
}

/** Whole-label match only (split on . and -). */
export function labelPornHit(host) {
  const h = normHost(host);
  const labels = h.split(/[.-]/).filter(Boolean);
  const want = seed.host_labels || [];
  for (let i = 0; i < labels.length; i++) {
    for (let j = 0; j < want.length; j++) {
      if (labels[i] === want[j]) return want[j];
    }
  }
  return "";
}

/**
 * @returns {"PORN_STRICT"|"SENSITIVE"|"WATCH"|"SOCIAL"|"SEARCH"|"TRUSTED"|"UGC"|"UNKNOWN"}
 */
export function hostTier(host) {
  const h = normHost(host);
  if (!h) return "UNKNOWN";
  const t = seed.tiers || {};
  if (suffixPorn(h) || labelPornHit(h) || listHas(h, t.PORN_STRICT) || inBlocklist(h)) {
    return "PORN_STRICT";
  }
  if (listHas(h, t.SENSITIVE)) return "SENSITIVE";
  if (listHas(h, t.WATCH)) return "WATCH";
  if (listHas(h, t.SOCIAL)) return "SOCIAL";
  if (listHas(h, t.SEARCH)) return "SEARCH";
  if (listHas(h, t.TRUSTED)) return "TRUSTED";
  if (listHas(h, t.UGC)) return "UGC";
  return "UNKNOWN";
}

export function getSeed() {
  return seed;
}

export { normHost, listHas, parentHosts };
