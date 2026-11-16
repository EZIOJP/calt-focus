/**
 * Appeal v2: deterministic auto-allow for SEARCH (clean query) + TRUSTED.
 * Everything else → cooldown queue. No 60s temp-allow. why text is never authority.
 */
import { GATE_V2 } from "./gate_config.js";
import { hostTier } from "./host_intel.js";
import {
  webSearchQuery,
  adultTokensInText,
  isRecoverySearchQuery,
  urlHasAdultTokens,
} from "./url_tokens.js";

const STORAGE_KEY = "gateAppealV2";

function pathKey(url) {
  try {
    const u = new URL(String(url || ""));
    return u.origin + u.pathname;
  } catch (e) {
    return String(url || "").split("?")[0];
  }
}

function todayKey() {
  const d = new Date();
  return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0");
}

export async function loadAppealState(storage) {
  try {
    const r = await storage.local.get(STORAGE_KEY);
    return r[STORAGE_KEY] || { grants: [], queue: [], day: todayKey(), dayCount: 0 };
  } catch (e) {
    return { grants: [], queue: [], day: todayKey(), dayCount: 0 };
  }
}

async function saveAppealState(storage, st) {
  await storage.local.set({ [STORAGE_KEY]: st });
}

function prune(st, now) {
  st.grants = (st.grants || []).filter((g) => g.until > now);
  st.queue = (st.queue || []).filter((q) => q.readyAt > now - GATE_V2.appeal.grantMs);
  if (st.day !== todayKey()) {
    st.day = todayKey();
    st.dayCount = 0;
  }
  return st;
}

export function isPathGranted(st, url, now) {
  const key = pathKey(url);
  const t = now || Date.now();
  for (let i = 0; i < (st.grants || []).length; i++) {
    if (st.grants[i].key === key && st.grants[i].until > t) return true;
  }
  return false;
}

/**
 * @returns {{ allow: boolean, why: string, until?: number, queued?: boolean, readyAt?: number }}
 */
export async function handleAppealRequest(storage, msg) {
  const now = Date.now();
  let st = prune(await loadAppealState(storage), now);
  const url = String(msg.url || "");
  const host = String(msg.host || "").toLowerCase().replace(/^www\./, "");
  const tier = hostTier(host);

  if (tier === "PORN_STRICT") {
    return { allow: false, why: "Known adult host — appeal denied." };
  }

  // Deterministic grace: TRUSTED always; SEARCH with non-adult / recovery query
  const q = webSearchQuery(url);
  if (tier === "TRUSTED" && !urlHasAdultTokens(url, msg.title || "")) {
    const until = now + GATE_V2.appeal.grantMs;
    st.grants.push({ key: pathKey(url), until, host, by: "trusted" });
    await saveAppealState(storage, st);
    return { allow: true, why: "Trusted host — opened for this path.", until };
  }
  if (tier === "SEARCH" && q !== null) {
    const adult = adultTokensInText(q).length > 0;
    if (!adult || isRecoverySearchQuery(q)) {
      const until = now + GATE_V2.appeal.grantMs;
      st.grants.push({ key: pathKey(url), until, host, by: "search" });
      await saveAppealState(storage, st);
      return {
        allow: true,
        why: adult ? "Recovery search — opened." : "Search query is not adult — opened.",
        until,
      };
    }
  }

  // Cooldown queue — note text ignored as authority
  if (st.dayCount >= GATE_V2.appeal.maxGrantsPerDay) {
    return { allow: false, why: "Daily appeal grant limit reached. Try tomorrow." };
  }

  const key = pathKey(url);
  const existing = (st.queue || []).find((x) => x.key === key);
  if (existing && existing.readyAt > now) {
    return {
      allow: false,
      queued: true,
      readyAt: existing.readyAt,
      why:
        "Review queued — available in " +
        Math.ceil((existing.readyAt - now) / 60000) +
        " min. Note text cannot skip the wait.",
    };
  }
  if (existing && existing.readyAt <= now) {
    if (st.dayCount >= GATE_V2.appeal.maxGrantsPerDay) {
      return { allow: false, why: "Daily appeal grant limit reached." };
    }
    const until = now + GATE_V2.appeal.grantMs;
    st.grants.push({ key, until, host, by: "cooldown" });
    st.queue = st.queue.filter((x) => x.key !== key);
    st.dayCount = (st.dayCount || 0) + 1;
    await saveAppealState(storage, st);
    return {
      allow: true,
      why: "Cooldown finished — 30 min path grant. Logged.",
      until,
    };
  }

  const readyAt = now + GATE_V2.appeal.cooldownMs;
  st.queue.push({ key, host, readyAt, note: String(msg.note || "").slice(0, 80) });
  await saveAppealState(storage, st);
  return {
    allow: false,
    queued: true,
    readyAt,
    why:
      "Queued for review (" +
      Math.round(GATE_V2.appeal.cooldownMs / 60000) +
      " min). Your note is logged but cannot unlock early.",
  };
}
