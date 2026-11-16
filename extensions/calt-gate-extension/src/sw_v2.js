/**
 * Gate v2 wiring inside the service worker (imported by background entry).
 */
import {
  GATE_V2,
  loadSavedThresholds,
  saveThresholds,
  snapshotThresholds,
} from "./gate_config.js";
import { decide, mergeMonotonic } from "./decision_engine.js";
import { unwrapDestinationUrl } from "./unwrap.js";
import { hostTier } from "./host_intel.js";
import { loadCachedBlocklist, refreshBlocklist, currentBlocklistSize } from "./blocklist.js";
import {
  pushVerdict,
  recentVerdicts,
  recentHighScores,
  persistVerdicts,
  loadVerdicts,
} from "./verdict_log.js";
import { handleAppealRequest, loadAppealState, isPathGranted } from "./appeal_v2.js";
import { isImageSearchUrl } from "./url_tokens.js";

const navState = new Map(); // tabId -> { navId, scoreVerdict, images }

function ext() {
  return typeof chrome !== "undefined" ? chrome : browser;
}

export async function gateV2Init() {
  const api = ext();
  await loadSavedThresholds(api.storage);
  await saveThresholds(api.storage, snapshotThresholds());
  await loadVerdicts(api.storage);
  await loadCachedBlocklist(api.storage);
  try {
    await ensureOffscreen();
  } catch (e) {
    /* optional until model bundled */
  }
  api.alarms.create("gateV2Blocklist", { periodInMinutes: GATE_V2.blocklist.alarmMinutes });
  api.alarms.onAlarm.addListener((a) => {
    if (a.name === "gateV2Blocklist") {
      void refreshBlocklist(api.storage).then(() => persistVerdicts(api.storage));
    }
  });
  void syncSafeSearchDnr(api);
  // Refresh once a day after start (non-blocking)
  setTimeout(() => {
    void refreshBlocklist(api.storage);
  }, 15000);
}

/** Force SafeSearch query params / YouTube restrict header via DNR. */
async function syncSafeSearchDnr(api) {
  if (!api.declarativeNetRequest || !api.declarativeNetRequest.updateDynamicRules) return;
  const base = 9600;
  const removeRuleIds = [];
  for (let i = 0; i < 12; i++) removeRuleIds.push(base + i);
  const addRules = [
    {
      id: base,
      priority: 50,
      action: {
        type: "redirect",
        redirect: { transform: { queryTransform: { addOrReplaceParams: [{ key: "safe", value: "active" }] } } },
      },
      condition: {
        urlFilter: "||google.",
        resourceTypes: ["main_frame"],
        requestDomains: ["google.com", "www.google.com", "google.co.in"],
      },
    },
    {
      id: base + 1,
      priority: 50,
      action: {
        type: "redirect",
        redirect: { transform: { queryTransform: { addOrReplaceParams: [{ key: "adlt", value: "strict" }] } } },
      },
      condition: { requestDomains: ["bing.com", "www.bing.com"], resourceTypes: ["main_frame"] },
    },
    {
      id: base + 2,
      priority: 50,
      action: {
        type: "redirect",
        redirect: { transform: { queryTransform: { addOrReplaceParams: [{ key: "kp", value: "1" }] } } },
      },
      condition: {
        requestDomains: ["duckduckgo.com", "www.duckduckgo.com"],
        resourceTypes: ["main_frame"],
      },
    },
    {
      id: base + 3,
      priority: 50,
      action: {
        type: "modifyHeaders",
        requestHeaders: [{ header: "YouTube-Restrict", operation: "set", value: "Strict" }],
      },
      condition: {
        requestDomains: ["youtube.com", "www.youtube.com", "m.youtube.com"],
        resourceTypes: ["main_frame", "sub_frame", "xmlhttprequest"],
      },
    },
  ];
  try {
    await api.declarativeNetRequest.updateDynamicRules({ removeRuleIds, addRules });
  } catch (e) {
    console.warn("CALT Gate SafeSearch DNR", e);
  }
}

async function ensureOffscreen() {
  const api = ext();
  if (!api.offscreen) return;
  const exists = await api.offscreen.hasDocument?.();
  if (exists) return;
  try {
    await api.offscreen.createDocument({
      url: "offscreen.html",
      reasons: ["BLOBS"],
      justification: "NSFWJS WASM image classification for CALT Gate",
    });
  } catch (e) {
    /* already exists or unsupported */
  }
}

export function gateV2OnNav(tabId, url, title, meta) {
  if (!GATE_V2.enabled) return null;
  const dest = unwrapDestinationUrl(url);
  const st = navState.get(tabId) || { navId: 0, images: {} };
  st.navId = (st.navId || 0) + 1;
  const navId = st.navId;
  st.url = dest;
  // Fresh page: never carry a prior LOCK/WATCH onto the next navigation
  // (e.g. YouTube lock sticking when the same tab opens scaler.com).
  st.images = {};
  st.scoreVerdict = null;
  navState.set(tabId, st);

  return evaluateTab(tabId, navId, dest, title || "", meta || {});
}

async function evaluateTab(tabId, navId, url, title, meta) {
  const api = ext();
  const st = navState.get(tabId);
  if (!st || st.navId !== navId) return null;

  let appealGrant = false;
  try {
    const ast = await loadAppealState(api.storage);
    appealGrant = isPathGranted(ast, url, Date.now());
  } catch (e) {
    appealGrant = false;
  }

  const input = {
    url,
    title,
    mode: meta.mode || "free",
    softlandEnabled: meta.softlandEnabled !== false,
    nativeReason: meta.nativeReason || "",
    contentScore: meta.contentScore || 0,
    contentAction: meta.contentAction || "",
    rta: Boolean(meta.rta),
    metaAdult: Boolean(meta.metaAdult),
    images: st.images || {},
    quinn: meta.quinn || null,
    appealGrant,
    priorScore: (st.scoreVerdict && st.scoreVerdict.score) || 0,
  };

  let verdict = decide(input);
  if (st.scoreVerdict) verdict = mergeMonotonic(st.scoreVerdict, verdict);
  st.scoreVerdict = verdict;
  navState.set(tabId, st);

  pushVerdict({
    ...verdict,
    mode: input.mode || "",
    thresholds: snapshotThresholds(),
  });
  void persistVerdicts(api.storage);

  if (GATE_V2.shadowOnly) return { ...verdict, shadow: true };

  if (verdict.verdict === "LOCK") {
    return verdict;
  }
  if (verdict.verdict === "WATCH") {
    try {
      api.tabs.sendMessage(tabId, {
        type: "GATE_V2_WATCH",
        blur: true,
        scannerOffline: Boolean((st.images || {}).scannerOffline),
        navId,
      });
    } catch (e) {
      /* no content script */
    }
  }
  return verdict;
}

export async function gateV2OnImageSignal(tabId, payload) {
  const st = navState.get(tabId);
  if (!st) return null;
  if (payload.navId != null && payload.navId !== st.navId) return null;
  const img = st.images || {};
  img.maxPornHentai = Math.max(img.maxPornHentai || 0, Number(payload.pornHentai) || 0);
  img.maxSexy = Math.max(img.maxSexy || 0, Number(payload.sexy) || 0);
  if ((Number(payload.pornHentai) || 0) >= GATE_V2.image.aggregateMin) {
    img.count035 = (img.count035 || 0) + 1;
  }
  if (payload.scannerOffline) img.scannerOffline = true;
  st.images = img;
  navState.set(tabId, st);
  return evaluateTab(tabId, st.navId, st.url || payload.url || "", payload.title || "", {
    mode: payload.mode,
    softlandEnabled: payload.softlandEnabled,
    nativeReason: payload.nativeReason,
  });
}

export async function gateV2OnContentScore(tabId, payload) {
  const st = navState.get(tabId);
  if (!st) return null;
  return evaluateTab(tabId, st.navId, payload.url || st.url || "", payload.title || "", {
    mode: payload.mode,
    softlandEnabled: payload.softlandEnabled,
    nativeReason: payload.nativeReason,
    contentScore: payload.score,
    contentAction: payload.action,
    rta: payload.rta,
    metaAdult: payload.metaAdult,
  });
}

/** Map Gate v2 hard token → locked.html ?why= (must not default to porn). */
export function gateV2WhyLabel(hard, reasons) {
  const h = String(hard || "").toLowerCase();
  if (h === "porn_strict" || h === "adult_search_query" || h === "rta") return "porn";
  if (h.indexOf("porn") >= 0) return "porn";
  if (h === "watch_study") return "study";
  if (h === "sensitive") return "sensitive";
  if (h.indexOf("morning") >= 0 || h === "bible" || h === "plan" || h.indexOf("native_") === 0)
    return h.replace(/^native_/, "") || "morning";
  if (h === "incubation") return "incubation";
  if (h) return h;
  const joined = (reasons || []).join(" ").toLowerCase();
  if (joined.indexOf("image_porn") >= 0 || joined.indexOf("content_lock") >= 0) return "keyword";
  if (joined.indexOf("watch") >= 0 || joined.indexOf("study") >= 0) return "study";
  return "blocked";
}

export function gateV2LockedUrl(extensionBase, fromUrl, reasons, hard) {
  const u = new URL(extensionBase + "locked.html");
  try {
    const parsed = new URL(fromUrl);
    u.searchParams.set("host", parsed.hostname.replace(/^www\./, ""));
  } catch (e) {
    u.searchParams.set("host", "");
  }
  u.searchParams.set("from", String(fromUrl || "").slice(0, 1800));
  u.searchParams.set("why", gateV2WhyLabel(hard, reasons));
  u.searchParams.set("v2", (reasons || []).slice(0, 8).join("|"));
  return u.toString();
}

export async function gateV2HandleMessage(msg, sender, sendResponse) {
  const api = ext();
  if (!msg || !msg.type) return false;

  if (msg.type === "GATE_V2_STATUS") {
    sendResponse({
      ok: true,
      version: GATE_V2.version,
      enabled: GATE_V2.enabled,
      shadowOnly: GATE_V2.shadowOnly,
      blocklist: currentBlocklistSize(),
      thresholds: snapshotThresholds(),
      recent: recentVerdicts(20),
      highScores: recentHighScores(20),
    });
    return true;
  }
  if (msg.type === "GATE_V2_WHY") {
    sendResponse({
      ok: true,
      recent: recentVerdicts(100),
      highScores: recentHighScores(100),
      thresholds: snapshotThresholds(),
    });
    return true;
  }
  if (msg.type === "GATE_V2_THRESHOLDS_GET") {
    sendResponse({ ok: true, thresholds: snapshotThresholds() });
    return true;
  }
  if (msg.type === "GATE_V2_THRESHOLDS_SET") {
    void saveThresholds(api.storage, msg.thresholds || msg).then((snap) => {
      sendResponse({ ok: true, thresholds: snap });
    });
    return true;
  }
  if (msg.type === "GATE_V2_REFRESH_BLOCKLIST") {
    void refreshBlocklist(api.storage).then((r) => sendResponse(r));
    return true;
  }
  if (msg.type === "PORN_APPEAL_REQUEST" || msg.type === "GATE_V2_APPEAL") {
    void handleAppealRequest(api.storage, msg).then((r) => sendResponse(r));
    return true;
  }
  if (msg.type === "GATE_V2_IMAGE_RESULT") {
    const tabId = sender.tab && sender.tab.id;
    void gateV2OnImageSignal(tabId, msg).then((v) => {
      if (v && v.verdict === "LOCK" && !GATE_V2.shadowOnly && typeof tabId === "number") {
        const target = gateV2LockedUrl(
          api.runtime.getURL(""),
          msg.url || (navState.get(tabId) || {}).url,
          v.reasons,
          v.hard,
        );
        void api.tabs.update(tabId, { url: target });
      }
      sendResponse({ ok: true, verdict: v });
    });
    return true;
  }
  if (msg.type === "GATE_V2_CONTENT") {
    const tabId = sender.tab && sender.tab.id;
    void gateV2OnContentScore(tabId, msg).then((v) => {
      if (v && v.verdict === "LOCK" && !GATE_V2.shadowOnly && typeof tabId === "number") {
        const target = gateV2LockedUrl(api.runtime.getURL(""), msg.url || "", v.reasons, v.hard);
        void api.tabs.update(tabId, { url: target });
      }
      sendResponse({ ok: true, verdict: v });
    });
    return true;
  }
  if (msg.type === "GATE_V2_CLASSIFY_URL") {
    void (async () => {
      await ensureOffscreen();
      try {
        const resp = await api.runtime.sendMessage({
          type: "OFFSCREEN_NSFW_CLASSIFY",
          dataUrl: msg.dataUrl,
          url: msg.url,
        });
        sendResponse(resp);
      } catch (e) {
        sendResponse({ ok: false, scannerOffline: true, error: String(e && e.message) });
      }
    })();
    return true;
  }
  return false;
}

export function gateV2ShouldBlurHost(host) {
  const t = hostTier(host);
  return t === "UGC" || t === "UNKNOWN" || t === "SOCIAL" || t === "SENSITIVE" || t === "WATCH";
}

export function gateV2WatchOnLoad(url) {
  return isImageSearchUrl(url) || gateV2ShouldBlurHost((() => {
    try {
      return new URL(url).hostname;
    } catch (e) {
      return "";
    }
  })());
}

export { GATE_V2, decide, hostTier };
