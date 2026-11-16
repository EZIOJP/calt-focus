// ============================================================
// CALT Gate — blocker only (MV3 DNR + light softLand)
// No telemetry / WebSocket. Pair with CALT SelfTracker.
// Watch hosts = DNR only. Other blocks = one-tab softLand.
// ============================================================

/* global GATE_API_URL, GATE_ALERT_URL, GATE_EXT_LOG_URL, GATE_ALERT_GAP_MS, FORCE_WATCH_HOSTS,
   FORCE_PORN_HOSTS, isStrictDayMode, TEMP_ALLOW_STORAGE_KEY, TEMP_ALLOW_MS, pruneTempAllows,
   buildTempAllowGrant, upsertTempAllow, isTempAllowExcludedHost, isHostTempAllowed,
   tempAllowUntilForHost, browserPolicyOrFallback, shouldBlockUrl, dnrHostList, listMatch,
   redirectTargetUrl, isCaltSpaUrl, blockKindForUrl, hostnameFromUrl,
   isForceWatchHost, isForcePornHost, classifyHostCategory, isExtensionOrInternalUrl,
   applyContentScoreFromPolicy, CALT_BIBLE_URL, CALT_PRODUCTIVITY_URL, isFreeDay,
   APPEAL_ALLOW_MS, APPEAL_ALLOW_STORAGE_KEY, QWEN_DEFAULT_BASE, QWEN_DEFAULT_MODEL,
   isAppealHardDenyHost, pruneAppealAllows, isHostAppealAllowed, upsertAppealAllow,
   isPornAppealReason, normalizeHost */

// Shared helpers are prepended by scripts/build_extension_workers.ps1 into service_worker.js

var DNR_WATCH_RULE_BASE = 9200;
/** Cap DNR rules — SoftLand covers any overflow from server watch_domains. */
var MAX_DNR_WATCH_HOSTS = 16;
/** Hard-block distraction hosts in every mode (including FREE). */
var DNR_PORN_RULE_BASE = 9300;
var MAX_DNR_PORN_HOSTS = 80;
/** Default poll; overwritten from browser.intervals.extension_gate_poll_s. */
var GATE_POLL_MS =
  typeof GATE_POLL_ACTIVE_S === "number" ? Math.max(4000, GATE_POLL_ACTIVE_S * 1000) : 4000;
var SOFTLAND_DEDUP_MS = 15000;

var extAPI = typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;

var gateCache = null;
var redirectsEnabled = true;
var lastGateFetchAt = 0;
var lastDnrFingerprint = "";
var lastDnrPornFingerprint = "";
/** Hosts currently covered by active DNR — SoftLand skips these only. */
var activeDnrWatchHosts = [];
var activeDnrPornHosts = [];
var lastAlertAt = 0;
var softLandDone = Object.create(null);
var softLandInFlight = Object.create(null);
var tempAllowsCache = [];
var appealAllowsCache = [];
var lastGateModeKey = "";
var softLandRecentAt = [];
var SOFTLAND_STORM_WINDOW_MS = 8000;
var SOFTLAND_STORM_MAX = 6;
var REDIRECT_COOLDOWN_MS = 30000;
var redirectCooldownUntil = 0;
var lastNotifyAt = 0;
var NOTIFY_GAP_MS = 8000;
/** Study :8000 side-channels (WS / alerts / HTTP gate). Default OFF. */
var studyHttpFallback = false;
/** Last native host health (ping / get_mode / get_softland_doc). */
var nativeHostOk = null;
/**
 * Secret Inspect mode — pause watch/social SoftLand redirects + watch DNR so overlays
 * can score pages. Porn / hard-deny hosts still lock (Inspect must not open tubes).
 */
var inspectModeCached = false;
/** Quinn appeals — Focus Home owns on/off via quinn_dashboard.json. Default ON. */
var quinnEnabledCached = true;
var lastQuinnSyncAt = 0;
var GET_MODE_TTL_MS = 2500;
var getModeCache = Object.create(null);
var nativePort = null;
var nativeQueue = [];
var nativeInFlight = false;
var gateBootstrapped = false;

var llmHostPrefs = {
  baseUrl: typeof QWEN_DEFAULT_BASE !== "undefined" ? QWEN_DEFAULT_BASE : "http://127.0.0.1:11434/v1",
  model: typeof QWEN_DEFAULT_MODEL !== "undefined" ? QWEN_DEFAULT_MODEL : "qwen2.5:3b",
  apiKey: "",
};

extAPI.storage.local.get(
  [
    "gateCache",
    "redirectsEnabled",
    "tempAllows",
    "appealAllows",
    "caltGateHttpFallback",
    "caltSoftlandHttpFallback",
    "caltInspectMode",
    "caltLlmHost",
    "caltQuinnEnabled",
  ],
  function (result) {
    if (result.gateCache) gateCache = result.gateCache;
    redirectsEnabled = true;
    if (result.redirectsEnabled === false) extAPI.storage.local.set({ redirectsEnabled: true });
    if (Array.isArray(result.tempAllows)) {
      tempAllowsCache =
        typeof pruneTempAllows === "function" ? pruneTempAllows(result.tempAllows, Date.now()) : result.tempAllows;
    }
    if (Array.isArray(result.appealAllows)) {
      appealAllowsCache =
        typeof pruneAppealAllows === "function"
          ? pruneAppealAllows(result.appealAllows, Date.now())
          : result.appealAllows;
    }
    // Focus product lock: never use Study :8000 SoftLand/gate HTTP (native msg_host only).
    studyHttpFallback = false;
    if (result.caltGateHttpFallback || result.caltSoftlandHttpFallback) {
      extAPI.storage.local.set({ caltGateHttpFallback: false, caltSoftlandHttpFallback: false });
    }
    inspectModeCached = Boolean(result.caltInspectMode);
    if (typeof result.caltQuinnEnabled === "boolean") quinnEnabledCached = result.caltQuinnEnabled;
    if (result.caltLlmHost && typeof result.caltLlmHost === "object") {
      llmHostPrefs = Object.assign({}, llmHostPrefs, result.caltLlmHost);
    }
    if (typeof CaltGateV2 !== "undefined" && CaltGateV2.init) {
      void CaltGateV2.init();
    }
  }
);

/**
 * Pull Quinn on/off from Focus mirror (msg_host get_quinn_prefs).
 * Gate popup no longer writes this — SoftLand blocking stays here; appeals toggle is Focus.
 */
async function syncQuinnFromFocus(opts) {
  opts = opts || {};
  var now = Date.now();
  if (!opts.force && lastQuinnSyncAt && now - lastQuinnSyncAt < 2000) {
    return quinnEnabledCached !== false;
  }
  lastQuinnSyncAt = now;
  try {
    var res = await nativeCall({ type: "get_quinn_prefs" }, 800);
    var en = true;
    if (res && typeof res.appeals_enabled === "boolean") {
      en = res.appeals_enabled;
    } else if (res && res.document && typeof res.document === "object" &&
               typeof res.document.appeals_enabled === "boolean") {
      en = res.document.appeals_enabled;
    } else if (res && typeof res.document === "string") {
      try {
        var doc = JSON.parse(res.document);
        if (typeof doc.appeals_enabled === "boolean") en = doc.appeals_enabled;
      } catch (eParse) {
        /* keep default */
      }
    } else if (res && typeof res.ok === "boolean" && res.ok === false) {
      return quinnEnabledCached !== false;
    }
    quinnEnabledCached = en !== false;
    try {
      extAPI.storage.local.set({ caltQuinnEnabled: quinnEnabledCached });
    } catch (eStore) {
      /* ignore */
    }
  } catch (e) {
    /* keep cached */
  }
  return quinnEnabledCached !== false;
}

function lockedPageUrl() {
  return extAPI.runtime.getURL("locked.html");
}

/** True if this tab is already on our lock interstitial (any query string). */
function tabUrlIsLockedPage(url) {
  return Boolean(url && String(url).indexOf("locked.html") >= 0);
}

/**
 * Hard-nav to lock page only when SoftLand skipped us AND the tab is not already locked.
 * Without this, softLandBlockedTab returns false on locked.html and callers re-tabs.update
 * with a different ?why= → infinite reload flicker.
 */
async function forceNavigateToLock(tabId, target) {
  if (tabId == null || !target) return false;
  try {
    var cur = await extAPI.tabs.get(tabId);
    var cu = cur && cur.url ? String(cur.url) : "";
    if (tabUrlIsLockedPage(cu)) return false;
    await extAPI.tabs.update(tabId, { url: target });
    return true;
  } catch (e) {
    return false;
  }
}

function lockedPageUrlForBlocked(blockedUrl, reason, untilIso) {
  var base = lockedPageUrl();
  var host = "";
  try {
    host = typeof hostnameFromUrl === "function" ? hostnameFromUrl(blockedUrl) : "";
  } catch (e) {
    host = "";
  }
  if (!host && !reason && !untilIso) return base;
  var q = host ? "host=" + encodeURIComponent(host) : "";
  try {
    if (blockedUrl && String(blockedUrl).indexOf("http") === 0) {
      q += (q ? "&" : "") + "from=" + encodeURIComponent(String(blockedUrl).slice(0, 500));
    }
    if (reason) {
      q += (q ? "&" : "") + "why=" + encodeURIComponent(String(reason).slice(0, 80));
    }
    // SoftLand get_mode until (ISO) — native path; not temp-allow until.
    if (untilIso) {
      q += (q ? "&" : "") + "until=" + encodeURIComponent(String(untilIso).slice(0, 40));
    }
  } catch (e2) {
    /* ignore */
  }
  return q ? base + "?" + q : base;
}

function gateCacheForBlockCheck() {
  if (!gateCache) return null;
  return Object.assign({}, gateCache, { temp_allows: tempAllowsCache, tempAllows: tempAllowsCache });
}

function reportGateAlert(kind, detail) {
  if (!studyHttpFallback) return;
  var now = Date.now();
  if (now - lastAlertAt < (GATE_ALERT_GAP_MS || 45000)) return;
  lastAlertAt = now;
  try {
    fetch(GATE_ALERT_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ kind: kind || "generic_rule_break", detail: detail || "" }),
      cache: "no-store",
    }).catch(function () {});
  } catch (e) {
    /* ignore */
  }
}

function logExtensionEvent(event, payload) {
  if (!studyHttpFallback) return;
  var url = typeof GATE_EXT_LOG_URL !== "undefined" ? GATE_EXT_LOG_URL : "";
  if (!url) return;
  try {
    fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(
        Object.assign({ event: event || "unknown", source: "calt-gate" }, payload || {})
      ),
      cache: "no-store",
    }).catch(function () {});
  } catch (e) {
    /* ignore */
  }
}

function invalidateGetModeCache(hostHint) {
  if (!hostHint) {
    getModeCache = Object.create(null);
    return;
  }
  var h = String(hostHint).toLowerCase().replace(/^www\./, "");
  Object.keys(getModeCache).forEach(function (k) {
    if (k.indexOf(h) >= 0) delete getModeCache[k];
  });
}

function getModeCacheKey(url) {
  try {
    var u = new URL(String(url || ""));
    return (u.hostname + u.pathname).toLowerCase().replace(/^www\./, "").slice(0, 220);
  } catch (e) {
    return String(url || "").slice(0, 220);
  }
}

function failNativeQueue() {
  nativeInFlight = false;
  try {
    if (nativePort) nativePort.disconnect();
  } catch (e) {}
  nativePort = null;
  nativeHostOk = false;
  var q = nativeQueue.splice(0, nativeQueue.length);
  for (var i = 0; i < q.length; i++) {
    try {
      clearTimeout(q[i].timer);
    } catch (e2) {}
    q[i].resolve(null);
  }
}

function pumpNativeQueue() {
  if (nativeInFlight || !nativeQueue.length) return;
  if (!extAPI.runtime || typeof extAPI.runtime.connectNative !== "function") {
    failNativeQueue();
    return;
  }
  var job = nativeQueue[0];
  nativeInFlight = true;
  try {
    if (!nativePort) {
      nativePort = extAPI.runtime.connectNative("com.calt.msg_host");
      // connectNative sets lastError when the host is already gone.
      var connectErr = extAPI.runtime.lastError;
      if (connectErr && connectErr.message) nativeHostOk = false;
      nativePort.onDisconnect.addListener(function () {
        // "Native host has exited" is lastError. Reading .message marks it checked.
        var discErr = extAPI.runtime && extAPI.runtime.lastError;
        if (discErr && discErr.message) nativeHostOk = false;
        if (!nativeInFlight && !nativeQueue.length) {
          nativePort = null;
          return;
        }
        failNativeQueue();
      });
    }
  } catch (e) {
    nativeQueue.shift();
    nativeInFlight = false;
    nativeHostOk = false;
    job.resolve(null);
    pumpNativeQueue();
    return;
  }
  var settled = false;
  var onMsg = null;
  function finish(msg, killPort) {
    if (settled) return;
    settled = true;
    try {
      clearTimeout(job.timer);
    } catch (e2) {}
    if (onMsg && nativePort) {
      try {
        nativePort.onMessage.removeListener(onMsg);
      } catch (e3) {}
    }
    if (nativeQueue[0] === job) nativeQueue.shift();
    nativeInFlight = false;
    if (killPort) {
      try {
        if (nativePort) nativePort.disconnect();
      } catch (e4) {}
      nativePort = null;
    }
    if (msg && typeof msg === "object") nativeHostOk = msg.ok !== false;
    else if (killPort) nativeHostOk = false;
    job.resolve(msg && typeof msg === "object" ? msg : null);
    pumpNativeQueue();
  }
  job.timer = setTimeout(function () {
    finish(null, true);
  }, job.timeoutMs || 1200);
  try {
    onMsg = function (msg) {
      finish(msg, false);
    };
    nativePort.onMessage.addListener(onMsg);
    nativePort.postMessage(job.message);
  } catch (e5) {
    finish(null, true);
  }
}

function nativeCall(message, timeoutMs) {
  return new Promise(function (resolve) {
    nativeQueue.push({
      message: message,
      timeoutMs: timeoutMs || 1200,
      resolve: resolve,
      timer: null,
    });
    pumpNativeQueue();
  });
}

async function softlandNativePing() {
  var msg = await nativeCall({ type: "ping", schema_version: 1 }, 800);
  nativeHostOk = Boolean(msg && msg.ok && msg.type === "pong");
  return nativeHostOk;
}

function redirectsPausedByCircuit() {
  return redirectCooldownUntil && Date.now() < redirectCooldownUntil;
}

function noteSoftLandAttempt() {
  var now = Date.now();
  softLandRecentAt.push(now);
  while (softLandRecentAt.length && now - softLandRecentAt[0] > SOFTLAND_STORM_WINDOW_MS) {
    softLandRecentAt.shift();
  }
  if (softLandRecentAt.length >= SOFTLAND_STORM_MAX) {
    redirectCooldownUntil = now + REDIRECT_COOLDOWN_MS;
    softLandRecentAt = [];
    logExtensionEvent("circuit_breaker", {
      detail: "softLand storm — redirects paused " + Math.round(REDIRECT_COOLDOWN_MS / 1000) + "s",
      notify: false,
    });
    console.warn("CALT Gate: softLand circuit breaker — redirects paused");
    return true;
  }
  return false;
}

function showBlockNotification(title, message) {
  var now = Date.now();
  if (now - lastNotifyAt < NOTIFY_GAP_MS) return;
  lastNotifyAt = now;
  if (!extAPI.notifications || !extAPI.notifications.create) return;
  try {
    var noteIcon = extAPI.runtime.getURL("icon.png");
    var noted = extAPI.notifications.create(
      "calt-gate-" + now,
      {
        type: "basic",
        iconUrl: noteIcon,
        title: title || "CALT blocked a webpage",
        message: message || "This site is SoftLand-blocked. Edge stayed open.",
        priority: 2,
        requireInteraction: false,
      },
      function () {
        var noteErr = extAPI.runtime && extAPI.runtime.lastError;
        if (noteErr && noteErr.message) return;
      }
    );
    // Edge returns a promise even with a callback. A missing icon rejects it.
    if (noted && typeof noted.then === "function") {
      noted.then(
        function () {},
        function () {}
      );
    }
  } catch (e) {
    /* ignore */
  }
}

function watchBlockActive() {
  if (!redirectsEnabled) return false;
  if (!gateCache || (!gateCache.ok && !gateCache.degraded)) return false;
  // Reward day / goal unlock must clear YouTube DNR (softLand alone is not enough).
  if (typeof isFreeDay === "function" && isFreeDay(gateCache)) return false;
  var browser = gateCache.browser || {};
  var mode = String(browser.mode || "").toLowerCase();
  if (mode === "free" || gateCache.day_unlimited || gateCache.reward_day || gateCache.unlocked) {
    return false;
  }
  if (browser.block_watch_sites) return true;
  return typeof isStrictDayMode === "function" && isStrictDayMode(mode);
}

async function syncDeclarativeWatchBlock(opts) {
  opts = opts || {};
  if (!extAPI.declarativeNetRequest || !extAPI.declarativeNetRequest.updateDynamicRules) {
    return;
  }
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var hosts =
    typeof dnrHostList === "function"
      ? dnrHostList(pol.force_watch_hosts || pol.watch_domains, pol.watch_domains, MAX_DNR_WATCH_HOSTS)
      : (pol.watch_domains || FORCE_WATCH_HOSTS || []).slice(0, MAX_DNR_WATCH_HOSTS);
  var active = watchBlockActive() && !inspectModeCached;
  var fingerprint =
    (active ? "1" : "0") + "|inspect:" + (inspectModeCached ? "1" : "0") + "|" + hosts.join(",");
  if (!opts.force && fingerprint === lastDnrFingerprint) return;

  var removeIds = [];
  for (var i = 0; i < MAX_DNR_WATCH_HOSTS + 4; i++) removeIds.push(DNR_WATCH_RULE_BASE + i);

  try {
    if (!active) {
      await extAPI.declarativeNetRequest.updateDynamicRules({
        removeRuleIds: removeIds,
        addRules: [],
      });
      lastDnrFingerprint = fingerprint;
      activeDnrWatchHosts = [];
      return;
    }
    var target = lockedPageUrl();
    var addRules = [];
    for (var j = 0; j < hosts.length; j++) {
      addRules.push({
        id: DNR_WATCH_RULE_BASE + j,
        priority: 100,
        action: { type: "redirect", redirect: { url: target } },
        condition: {
          requestDomains: [hosts[j]],
          resourceTypes: ["main_frame"],
        },
      });
    }
    await extAPI.declarativeNetRequest.updateDynamicRules({
      removeRuleIds: removeIds,
      addRules: addRules,
    });
    lastDnrFingerprint = fingerprint;
    activeDnrWatchHosts = hosts.slice();
  } catch (e) {
    console.warn("CALT Gate: DNR watch sync failed", e);
  }
}

/** Always-on DNR for distraction hosts (FREE + study). SoftLand is backup only. */
async function syncDeclarativePornBlock(opts) {
  opts = opts || {};
  if (!extAPI.declarativeNetRequest || !extAPI.declarativeNetRequest.updateDynamicRules) {
    return;
  }
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  // forcePornList unions SoftLand payload + offline seeds (sxyprn, etc.).
  var pornSeed =
    typeof forcePornList === "function"
      ? forcePornList(pol)
      : pol.force_porn_hosts || pol.porn_domains || FORCE_PORN_HOSTS || [];
  var hosts =
    typeof dnrHostList === "function"
      ? dnrHostList(
          typeof FORCE_SENSITIVE_HOSTS !== "undefined" ? FORCE_SENSITIVE_HOSTS : [],
          pornSeed,
          MAX_DNR_PORN_HOSTS
        )
      : pornSeed.slice(0, MAX_DNR_PORN_HOSTS);
  // Porn has no exceptions — do not strip DNR hosts for Quinn appeal grants.
  appealAllowsCache =
    typeof pruneAppealAllows === "function"
      ? pruneAppealAllows(appealAllowsCache, Date.now())
      : appealAllowsCache || [];
  // Porn DNR is always on (commitment). Inspect + "Toggle redirects" only pause
  // SoftLand watch/social redirects — not adult host DNR.
  var active = true;
  var appealFp = appealAllowsCache
    .map(function (e) {
      return e.host + "@" + e.until;
    })
    .join(",");
  var fingerprint =
    (active ? "1" : "0") +
    "|porn|inspect:" +
    (inspectModeCached ? "1" : "0") +
    "|appeal:" +
    appealFp +
    "|" +
    hosts.join(",");
  if (!opts.force && fingerprint === lastDnrPornFingerprint) return;

  var removeIds = [];
  for (var i = 0; i < MAX_DNR_PORN_HOSTS + 4; i++) removeIds.push(DNR_PORN_RULE_BASE + i);

  try {
    if (!active) {
      await extAPI.declarativeNetRequest.updateDynamicRules({
        removeRuleIds: removeIds,
        addRules: [],
      });
      lastDnrPornFingerprint = fingerprint;
      activeDnrPornHosts = [];
      return;
    }
    var target = lockedPageUrl();
    var addRules = [];
    for (var j = 0; j < hosts.length; j++) {
      addRules.push({
        id: DNR_PORN_RULE_BASE + j,
        priority: 110,
        action: { type: "redirect", redirect: { url: target } },
        condition: {
          requestDomains: [hosts[j]],
          resourceTypes: ["main_frame"],
        },
      });
    }
    await extAPI.declarativeNetRequest.updateDynamicRules({
      removeRuleIds: removeIds,
      addRules: addRules,
    });
    lastDnrPornFingerprint = fingerprint;
    activeDnrPornHosts = hosts.slice();
  } catch (e) {
    console.warn("CALT Gate: DNR distraction sync failed", e);
  }
}

function hostCoveredByActiveDnr(host) {
  if (!host) return false;
  if (typeof listMatch === "function") {
    if (activeDnrWatchHosts.length && listMatch(host, activeDnrWatchHosts)) return true;
    if (activeDnrPornHosts.length && listMatch(host, activeDnrPornHosts)) return true;
    return false;
  }
  return false;
}

function caltExtensionHeaders() {
  var mode = "";
  try {
    mode = String((gateCache && gateCache.browser && gateCache.browser.mode) || "");
  } catch (e) {
    mode = "";
  }
  var paused = false;
  try {
    paused = typeof redirectsPausedByCircuit === "function" && redirectsPausedByCircuit();
  } catch (e2) {
    paused = false;
  }
  return {
    "X-CALT-Extension": "calt-gate",
    "X-CALT-Ext-Mode": mode,
    "X-CALT-Ext-Circuit": paused ? "1" : "0",
    "X-CALT-Ext-Paused": redirectsEnabled === false ? "1" : "0",
  };
}

var gateFetchBackoffMs = 0;
var gateNotifyWs = null;

function noteGateFetchSuccess() {
  gateFetchBackoffMs = 0;
}

function noteGateFetchFailure() {
  if (!gateFetchBackoffMs) gateFetchBackoffMs = 30000;
  else gateFetchBackoffMs = Math.min(300000, gateFetchBackoffMs * 2);
  lastGateFetchAt = Date.now();
}

function connectGateNotifyWs() {
  // Study gate notify WS — opt-in only (same flag as HTTP distraction-gate).
  if (!studyHttpFallback) return;
  var url = typeof GATE_NOTIFY_WS_URL !== "undefined" ? GATE_NOTIFY_WS_URL : "ws://127.0.0.1:8000/ws/gate";
  if (gateNotifyWs && (gateNotifyWs.readyState === WebSocket.OPEN || gateNotifyWs.readyState === WebSocket.CONNECTING)) {
    return;
  }
  try {
    gateNotifyWs = new WebSocket(url);
    gateNotifyWs.onmessage = function (ev) {
      try {
        var msg = JSON.parse(ev.data);
        if (msg && msg.type === "GATE_CHANGED") {
          void pollGate({ force: true });
        }
      } catch (e) {
        /* ignore */
      }
    };
    gateNotifyWs.onclose = function () {
      gateNotifyWs = null;
      if (!studyHttpFallback) return;
      try {
        extAPI.alarms.create("gate-ws-retry", { delayInMinutes: 1 });
      } catch (e2) {
        /* ignore */
      }
    };
    gateNotifyWs.onerror = function () {
      try {
        gateNotifyWs.close();
      } catch (e3) {
        /* ignore */
      }
      gateNotifyWs = null;
    };
  } catch (e) {
    console.warn("CALT Gate: notify WS unavailable", e);
  }
}

async function softlandNativeGetDoc() {
  return nativeCall({ type: "get_softland_doc", schema_version: 1 }, 1200);
}

async function applyGatePayload(g, opts) {
  opts = opts || {};
  lastGateFetchAt = Date.now();
  noteGateFetchSuccess();
  var morning = g.morning || {};
  var browser = g.browser || {};
  gateCache = {
    ok: true,
    stale: false,
    degraded: false,
    source: g.source || opts.source || "unknown",
    fetched_at: lastGateFetchAt,
    policy_gen: g.policy_gen ?? null,
    locked: Boolean(g.locked),
    unlocked: Boolean(g.unlocked),
    enabled: Boolean(g.enabled),
    remaining_minutes: g.remaining_minutes ?? null,
    chapter_goal_met: Boolean(g.chapter_goal_met),
    day_unlimited: Boolean(g.day_unlimited),
    day_pass: Boolean(g.day_pass),
    reward_day: Boolean(g.reward_day),
    productive_minutes: g.productive_minutes ?? 0,
    daily_goal_minutes: g.daily_goal_minutes ?? 0,
    suggested_links: Array.isArray(g.suggested_links) ? g.suggested_links : [],
    current_block: g.current_block || null,
    morning: {
      next: morning.next || "open",
      bible_done: Boolean(morning.bible_done),
      plan_done: Boolean(morning.plan_done),
      bible_url: morning.bible_url || null,
      plan_url: morning.plan_url || null,
      redirect_url: morning.redirect_url || null,
      hint: morning.hint || "",
    },
    browser: browser,
    enforce: Boolean(browser.enforce) || Boolean(g.locked),
  };
  await extAPI.storage.local.set({ gateCache: gateCache });
  if (typeof applyContentScoreFromPolicy === "function") {
    applyContentScoreFromPolicy(browserPolicyOrFallback(browser));
  }
  var modeNow = String(browser.mode || "").toLowerCase();
  var freeNow =
    Boolean(g.day_unlimited) || Boolean(g.reward_day) || modeNow === "free";
  var modeKey =
    modeNow +
    "|" +
    (freeNow ? "1" : "0") +
    "|" +
    Boolean(browser.block_watch_sites) +
    "|" +
    Boolean(g.locked);
  var forceDnr = modeKey !== lastGateModeKey;
  if (forceDnr) lastGateModeKey = modeKey;
  await syncDeclarativeWatchBlock({ force: forceDnr || opts.force });
  await syncDeclarativePornBlock({ force: forceDnr || opts.force });
  scheduleGatePollAlarm();
}

async function pollGate(opts) {
  opts = opts || {};
  var now = Date.now();
  if (!opts.force && lastGateFetchAt && now - lastGateFetchAt < desiredGatePollMs() - 500) return;
  void syncQuinnFromFocus({ force: Boolean(opts.force) });
  try {
    var native = await softlandNativeGetDoc();
    if (native && native.ok !== false && (native.browser || native.enabled !== undefined)) {
      await applyGatePayload(native, { force: opts.force, source: "native" });
      return;
    }

    // Focus product lock: Study :8000 gate HTTP stays off.
    studyHttpFallback = false;
    if (!studyHttpFallback) {
      throw new Error(
        native && native.error
          ? "native:" + native.error
          : "native_softland_doc_unavailable"
      );
    }

    var r = await fetch(GATE_API_URL, { cache: "no-store", headers: caltExtensionHeaders() });
    if (!r.ok) throw new Error("HTTP " + r.status);
    var g = await r.json();
    g.source = "http";
    await applyGatePayload(g, { force: opts.force, source: "http" });
  } catch (e) {
    noteGateFetchFailure();
    scheduleGatePollAlarm();
    var err = String(e && e.message ? e.message : e);
    var prev = gateCache && typeof gateCache === "object" ? gateCache : null;
    var prevBrowser = (prev && prev.browser) || {};
    var prevMode = String(prevBrowser.mode || "").toLowerCase();
    var prevFree =
      prevMode === "free" ||
      Boolean(prev && (prev.day_unlimited || prev.reward_day || prev.unlocked));
    if (prev && (prev.ok || prev.degraded)) {
      gateCache = Object.assign({}, prev, {
        ok: true,
        stale: true,
        degraded: true,
        fetched_at: Date.now(),
        error: err,
        day_unlimited: Boolean(prev.day_unlimited) || prevFree,
        reward_day: Boolean(prev.reward_day),
        unlocked: Boolean(prev.unlocked) || prevFree,
        browser: Object.assign({}, prevBrowser, {
          block_watch_sites: prevFree ? false : true,
          block_social: prevFree ? false : prevBrowser.block_social === true,
          block_other: prevFree ? false : true,
          block_porn: prevBrowser.block_porn !== false,
          mode: prevFree ? "free" : prevMode || "study",
          mode_label: prevFree ? "FREE" : prevBrowser.mode_label || "STUDY",
          enforce: true,
        }),
      });
    } else {
      gateCache = {
        ok: true,
        stale: true,
        degraded: true,
        fetched_at: Date.now(),
        locked: true,
        unlocked: false,
        enabled: true,
        enforce: true,
        error: err,
        morning: { next: "open", bible_done: false, plan_done: false },
        browser: {
          mode: "study",
          mode_label: "STUDY",
          enforce: true,
          block_watch_sites: true,
          block_social: true,
          block_other: true,
          block_porn: true,
          block_keywords: true,
        },
      };
    }
    await extAPI.storage.local.set({ gateCache: gateCache });
    await syncDeclarativeWatchBlock({ force: true });
    await syncDeclarativePornBlock({ force: true });
  }
}

function desiredGatePollMs() {
  var s = typeof GATE_POLL_ACTIVE_S === "number" ? GATE_POLL_ACTIVE_S : 300;
  try {
    var iv = gateCache && gateCache.browser && gateCache.browser.intervals;
    if (iv && Number(iv.extension_gate_poll_s) > 0) {
      s = Number(iv.extension_gate_poll_s);
    }
  } catch (e) {
    /* ignore */
  }
  var base = Math.max(4000, Math.min(600000, Math.round(s * 1000)));
  if (gateFetchBackoffMs > base) return gateFetchBackoffMs;
  return base;
}

/** MV3-friendly: chrome.alarms only (no setInterval dual-poll). */
function scheduleGatePollAlarm() {
  var ms = desiredGatePollMs();
  GATE_POLL_MS = ms;
  var mins = Math.max(1, Math.min(10, Math.round(ms / 60000) || 5));
  try {
    extAPI.alarms.create("gate-poll", { periodInMinutes: mins });
  } catch (e) {
    /* ignore */
  }
}

function bootstrapGate() {
  if (gateBootstrapped) {
    scheduleGatePollAlarm();
    return;
  }
  gateBootstrapped = true;
  scheduleGatePollAlarm();
  void softlandNativePing();
  void pollGate({ force: true });
  if (studyHttpFallback) connectGateNotifyWs();
}

extAPI.alarms.onAlarm.addListener(function (alarm) {
  if (alarm.name === "gate-poll") void pollGate({ force: true });
  if (alarm.name === "gate-ws-retry" && studyHttpFallback) connectGateNotifyWs();
});

extAPI.runtime.onInstalled.addListener(function () {
  bootstrapGate();
});
extAPI.runtime.onStartup.addListener(function () {
  bootstrapGate();
});

bootstrapGate();

async function softLandBlockedTab(tabId, spaUrl, meta) {
  meta = meta || {};
  var kindMeta = String(meta.kind || "").toLowerCase();
  var hardKind =
    kindMeta.indexOf("porn") >= 0 ||
    kindMeta.indexOf("morning") >= 0 ||
    kindMeta === "keyword" ||
    kindMeta === "content_score" ||
    kindMeta.indexOf("content_score") >= 0 ||
    (meta.fromUrl && typeof isHardPornUrl === "function" && isHardPornUrl(meta.fromUrl)) ||
    (meta.host && typeof isHardPornUrl === "function" && isHardPornUrl("https://" + meta.host + "/")) ||
    (meta.fromUrl && typeof urlLooksAdult === "function" && urlLooksAdult(meta.fromUrl, ""));
  if (tabId == null) return false;
  if ((!redirectsEnabled && !hardKind) || (redirectsPausedByCircuit() && !hardKind)) return false;
  var mode = "";
  var next = "";
  try {
    mode = String((gateCache && gateCache.browser && gateCache.browser.mode) || "").toLowerCase();
    next = String((gateCache && gateCache.morning && gateCache.morning.next) || "").toLowerCase();
  } catch (e) {
    /* ignore */
  }
  var morningSoft =
    next === "bible" || next === "plan" || mode === "bible" || mode === "planning";
  var target = lockedPageUrl();
  if (morningSoft && spaUrl && String(spaUrl).indexOf("locked.html") < 0) {
    target = spaUrl;
  } else if (spaUrl && String(spaUrl).indexOf("locked.html") >= 0) {
    target = spaUrl;
  }
  if (softLandInFlight[tabId] === target) return false;
  var done = softLandDone[tabId];
  if (done && done.target === target && Date.now() - done.at < SOFTLAND_DEDUP_MS) return false;
  try {
    var existing = await extAPI.tabs.get(tabId);
    if (!existing) return false;
    var eu = existing.url ? String(existing.url) : "";
    if (tabUrlIsLockedPage(eu)) {
      // Already locked — treat as success so callers do NOT force-navigate again
      // (different ?why= / ?from= was causing the lock-page flicker loop).
      if (!morningSoft || tabUrlIsLockedPage(target)) {
        softLandDone[tabId] = { target: eu, at: Date.now() };
        return true;
      }
    }
    if (eu && typeof isCaltSpaUrl === "function" && isCaltSpaUrl(eu) && morningSoft) {
      softLandDone[tabId] = { target: target, at: Date.now() };
      return false;
    }
    // Circuit breaker must not swallow porn / content_score / morning locks.
    if (!hardKind && noteSoftLandAttempt()) return false;
    if (hardKind) noteSoftLandAttempt();
    softLandInFlight[tabId] = target;
    var blockedUrl = meta.fromUrl || eu || "";
    logExtensionEvent("soft_land", {
      kind: meta.kind || "blocked",
      detail: blockedUrl.slice(0, 200),
      url: blockedUrl.slice(0, 400),
      target: target.slice(0, 400),
      tab_id: tabId,
      notify: true,
    });
    var why = meta.kind ? String(meta.kind) : "blocked";
    var site = meta.host || blockedUrl.slice(0, 80) || "site";
    showBlockNotification(
      "CALT blocked a webpage",
      site + " · " + why + " — SoftLand redirected this tab (browser stayed open)."
    );
    await extAPI.tabs.update(tabId, { url: target });
    softLandDone[tabId] = { target: target, at: Date.now() };
    return true;
  } catch (e) {
    return false;
  } finally {
    delete softLandInFlight[tabId];
  }
}

async function softlandNativeGetMode(url) {
  var key = getModeCacheKey(url);
  var hit = getModeCache[key];
  var now = Date.now();
  if (hit && now - hit.at < GET_MODE_TTL_MS) return hit.msg;
  var msg = await nativeCall(
    {
      type: "get_mode",
      schema_version: 1,
      url: String(url || ""),
      tab_id: null,
      now: null,
    },
    900
  );
  if (msg && typeof msg === "object") {
    getModeCache[key] = { at: now, msg: msg };
  }
  return msg;
}

function inspectAllowsSoftlandKind(host, reason) {
  if (!inspectModeCached) return true;
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var r = String(reason || "").toLowerCase();
  // Morning gates + porn always enforce — Inspect must not open adult sites.
  if (r.indexOf("morning") >= 0 || r === "bible" || r === "plan") return true;
  if (r === "porn" || r === "keyword" || r.indexOf("porn") >= 0) return true;
  if (host && typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, pol)) return true;
  if (host && typeof isForcePornHost === "function" && isForcePornHost(host, pol)) return true;
  // Inspect: keep non-porn pages (YouTube etc.) so content-score overlays can paint.
  return false;
}

function isHardPornUrl(url) {
  var host = "";
  try {
    host = typeof hostnameFromUrl === "function" ? hostnameFromUrl(url) : "";
  } catch (e) {
    host = "";
  }
  if (!host) return false;
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  if (typeof isForcePornHost === "function" && isForcePornHost(host, pol)) return true;
  if (typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, pol)) return true;
  return false;
}

async function maybeRedirectTab(tabId, url, title) {
  // Must run before Gate v2 — webNavigation also fires for locked.html itself.
  // Re-scoring the interstitial + force tabs.update caused the lock-page flicker.
  if (!url) return false;
  if (tabUrlIsLockedPage(url)) return false;
  if (typeof isExtensionOrInternalUrl === "function" && isExtensionOrInternalUrl(url)) return false;
  if (typeof isCaltSpaUrl === "function" && isCaltSpaUrl(url)) return false;

  // Gate v2 scored pipeline (strict + grace). When it returns LOCK, redirect.
  if (typeof CaltGateV2 !== "undefined" && CaltGateV2.GATE_V2 && CaltGateV2.GATE_V2.enabled) {
    try {
      var modeV2 = String((gateCache && gateCache.browser && gateCache.browser.mode) || "free");
      var softOn =
        !(gateCache && gateCache.browser && gateCache.browser.softland_enabled === false);
      var v2 = CaltGateV2.onNav(tabId, url, title || "", {
        mode: modeV2,
        softlandEnabled: softOn,
        nativeReason: "",
      });
      if (v2 && typeof v2.then === "function") v2 = await v2;
      if (v2 && v2.verdict === "LOCK" && !v2.shadow) {
        var kindV2 = v2.hard || "blocked";
        var spaV2 = CaltGateV2.lockedUrl(
          extAPI.runtime.getURL(""),
          url,
          v2.reasons || [],
          kindV2,
        );
        reportGateAlert(kindV2, url.slice(0, 120));
        var lockedV2 = await softLandBlockedTab(tabId, spaV2, {
          fromUrl: url,
          kind: kindV2,
          host: v2.host || "",
        });
        if (!lockedV2) await forceNavigateToLock(tabId, spaV2);
        return true;
      }
      // WATCH / ALLOW — continue to native SoftLand for morning/watch; v2 already messaged blur.
    } catch (eGateV2) {
      console.warn("CALT Gate v2 nav", eGateV2);
    }
  }

  var host = "";
  try {
    host = typeof hostnameFromUrl === "function" ? hostnameFromUrl(url) : "";
  } catch (e) {
    host = "";
  }
  var polDirect =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var sensitiveDirect =
    host &&
    polDirect.block_sensitive !== false &&
    typeof isSensitiveHost === "function" &&
    isSensitiveHost(host, polDirect);
  // Watch redirects can pause. Porn and sensitive hosts (Grok, Reddit, Venice) still redirect.
  var hardPorn = isHardPornUrl(url);
  if (!redirectsEnabled && !hardPorn && !sensitiveDirect) return false;

  if (sensitiveDirect) {
    reportGateAlert("sensitive", url.slice(0, 120));
    var spaSens = lockedPageUrlForBlocked(url, "sensitive");
    var lockedSens = await softLandBlockedTab(tabId, spaSens, {
      fromUrl: url,
      kind: "sensitive",
      host: host,
    });
    if (!lockedSens) await forceNavigateToLock(tabId, spaSens);
    return true;
  }
  // Multi-stage adult path:
  // 1) known porn / sensitive hosts → lock now
  // 2) allow-listed + search hosts → never instant-lock from URL tokens; page must load
  // 3) content_score text → Quinn sentence confirm → optional screenshot
  // 4) appeal / temp-allow beat URL keyword locks (not hard-deny tubes)
  var allowResearchHost =
    host && typeof isFallbackAllowHost === "function" && isFallbackAllowHost(host) && !hardPorn;
  var searchPage = typeof webSearchQuery === "function" && webSearchQuery(url) !== null;
  var adultUrl = hardPorn;
  if (!adultUrl && !allowResearchHost && !searchPage) {
    if (typeof urlLooksAdult === "function") adultUrl = urlLooksAdult(url, title);
    else if (typeof urlLooksExplicit === "function") adultUrl = urlLooksExplicit(url, title);
  }

  // Quinn appeal grant (false-positive valve) — not for force/hard-deny tubes.
  var polAppeal =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var hardDenyHost =
    host &&
    typeof isAppealHardDenyHost === "function" &&
    isAppealHardDenyHost(host, polAppeal);
  if (
    host &&
    !hardDenyHost &&
    typeof isHostAppealAllowed === "function" &&
    isHostAppealAllowed(host, appealAllowsCache, Date.now())
  ) {
    return false;
  }
  if (host && hostCoveredByActiveDnr(host)) {
    return false;
  }

  // Temp allow beats URL keyword locks on non-hard-deny hosts (60s reopen path).
  if (host && !hardDenyHost && typeof isHostTempAllowed === "function") {
    var pol =
      typeof browserPolicyOrFallback === "function"
        ? browserPolicyOrFallback(gateCache && gateCache.browser)
        : {};
    if (isHostTempAllowed(host, tempAllowsCache, Date.now(), pol)) {
      return false;
    }
  }

  // Unknown mirror cheat: adult words in path/query/title even if host is not seeded.
  if (adultUrl) {
    reportGateAlert("porn", url.slice(0, 120));
    var spaUrlAdult = lockedPageUrlForBlocked(url, "porn");
    var lockedAdult = await softLandBlockedTab(tabId, spaUrlAdult, {
      fromUrl: url,
      kind: "porn",
      host: host || "",
    });
    if (!lockedAdult) await forceNavigateToLock(tabId, spaUrlAdult);
    return true;
  }

  // Native SoftLand only — if msg_host fails, no SoftLand redirect (DNR still covers lists).
  var native = await softlandNativeGetMode(url);
  if (native && native.ok !== false && native.action) {
    if (native.action === "allow" || native.action === "none") return false;
    if (native.action === "block" && native.enforce !== false) {
      var kindN = native.reason || "blocked";
      if (!inspectAllowsSoftlandKind(host, kindN)) return false;
      reportGateAlert(kindN, url.slice(0, 120));
      var spaN = lockedPageUrlForBlocked(url, kindN, native.until || "");
      return softLandBlockedTab(tabId, spaN, {
        fromUrl: url,
        kind: kindN,
        host: host || native.matched || "",
        until: native.until || "",
      });
    }
  }
  return false;
}

if (extAPI.webNavigation && extAPI.webNavigation.onCommitted) {
  extAPI.webNavigation.onCommitted.addListener(function (details) {
    if (details.frameId !== 0) return;
    if (!details.url || !details.tabId) return;
    void maybeRedirectTab(details.tabId, details.url, "");
  });
}
if (extAPI.webNavigation && extAPI.webNavigation.onHistoryStateUpdated) {
  extAPI.webNavigation.onHistoryStateUpdated.addListener(function (details) {
    if (details.frameId !== 0) return;
    if (!details.url || !details.tabId) return;
    void maybeRedirectTab(details.tabId, details.url, "");
  });
}
if (extAPI.webNavigation && extAPI.webNavigation.onBeforeNavigate) {
  extAPI.webNavigation.onBeforeNavigate.addListener(function (details) {
    if (details.frameId !== 0) return;
    if (!details.url || !details.tabId) return;
    void maybeRedirectTab(details.tabId, details.url, "");
  });
}

if (extAPI.tabs && extAPI.tabs.onRemoved) {
  extAPI.tabs.onRemoved.addListener(function (tabId) {
    delete softLandDone[tabId];
    delete softLandInFlight[tabId];
  });
}

function pruneAndLoadTempAllows(cb) {
  extAPI.storage.local.get([TEMP_ALLOW_STORAGE_KEY || "tempAllows"], function (res) {
    var list = Array.isArray(res.tempAllows) ? res.tempAllows : [];
    list = typeof pruneTempAllows === "function" ? pruneTempAllows(list, Date.now()) : list;
    cb(list);
  });
}

extAPI.runtime.onMessage.addListener(function (msg, sender, sendResponse) {
  if (!msg || !msg.type) return false;

  // Gate v2 messages (appeal, classify, why panel, image signals)
  if (typeof CaltGateV2 !== "undefined" && CaltGateV2.handleMessage) {
    if (
      String(msg.type).indexOf("GATE_V2_") === 0 ||
      msg.type === "PORN_APPEAL_REQUEST" ||
      msg.type === "OFFSCREEN_NSFW_CLASSIFY"
    ) {
      if (msg.type === "PORN_APPEAL_REQUEST" && CaltGateV2.GATE_V2 && CaltGateV2.GATE_V2.enabled) {
        var handled = CaltGateV2.handleMessage(msg, sender, sendResponse);
        if (handled) return true;
      } else if (msg.type !== "PORN_APPEAL_REQUEST") {
        var handledV2 = CaltGateV2.handleMessage(msg, sender, sendResponse);
        if (handledV2) return true;
      }
    }
  }
  if (msg.type === "GATE_V2_FETCH_IMAGE") {
    void (async function () {
      try {
        var res = await fetch(String(msg.url || ""), {
          credentials: "omit",
          signal: AbortSignal.timeout(8000),
        });
        if (!res.ok) {
          sendResponse({ ok: false, error: "http_" + res.status });
          return;
        }
        var buf = await res.arrayBuffer();
        if (buf.byteLength > 4 * 1024 * 1024) {
          sendResponse({ ok: false, error: "too_large" });
          return;
        }
        var bytes = new Uint8Array(buf);
        var bin = "";
        for (var i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
        var b64 = btoa(bin);
        var ct = String(res.headers.get("content-type") || "image/jpeg").split(";")[0];
        sendResponse({ ok: true, dataUrl: "data:" + ct + ";base64," + b64 });
      } catch (eFetch) {
        sendResponse({ ok: false, scannerOffline: true, error: String(eFetch && eFetch.message) });
      }
    })();
    return true;
  }
  if (
    msg.type === "TEMP_ALLOW_REQUEST" &&
    typeof CaltGateV2 !== "undefined" &&
    CaltGateV2.GATE_V2 &&
    CaltGateV2.GATE_V2.enabled
  ) {
    sendResponse({
      ok: false,
      error: "Temp allow removed in Gate v2 — use Request review (cooldown).",
    });
    return true;
  }

  if (msg.type === "CONTENT_SCORE_WARN") {
    return false;
  }
  if (msg.type === "CONTENT_SCORE_LOCK") {
    var lockUrl = String(msg.url || "");
    var tabId = sender && sender.tab && sender.tab.id;
    var excerptCs = String(msg.excerpt || "").slice(0, 1800);
    var matchedCs = Array.isArray(msg.matched) ? msg.matched.slice(0, 12) : [];
    var titleCs = String(msg.title || "").slice(0, 200);
    var scoreCs = Number(msg.score) || 0;
    void (async function () {
      var hostPeek = "";
      try {
        hostPeek = typeof hostnameFromUrl === "function" ? hostnameFromUrl(lockUrl) : "";
      } catch (ePeek) {
        hostPeek = "";
      }
      var polPeek =
        typeof browserPolicyOrFallback === "function"
          ? browserPolicyOrFallback(gateCache && gateCache.browser)
          : {};
      var modeCs = "free";
      try {
        var mCs = String((gateCache && gateCache.browser && gateCache.browser.mode) || "").toLowerCase();
        modeCs = mCs === "study" || mCs === "bible" || mCs === "planning" ? "study" : "free";
      } catch (eMode) {
        modeCs = "free";
      }
      var thCs =
        typeof CONTENT_SCORE_THRESHOLDS !== "undefined" && CONTENT_SCORE_THRESHOLDS[modeCs]
          ? CONTENT_SCORE_THRESHOLDS[modeCs]
          : { warn: 8, lock: 10 };
      var lockFloor = Number(thCs.lock) || 10;
      var warnFloor = Number(thCs.warn) || 8;

      // Unknown mirrors: do not depend on SoftLand host seeds.
      // Allow-listed research hosts ignore the bare word "porn" in the URL.
      var researchPeek =
        hostPeek && typeof isFallbackAllowHost === "function" && isFallbackAllowHost(hostPeek);
      var searchPagePeek =
        typeof webSearchQuery === "function" && webSearchQuery(lockUrl) !== null;
      // Search / allow-list: URL tokens alone are not a hard lock — text + Quinn + image decide.
      var listHard =
        (!(researchPeek || searchPagePeek) &&
          ((typeof urlLooksAdult === "function" && urlLooksAdult(lockUrl, titleCs)) ||
            (typeof urlLooksExplicit === "function" && urlLooksExplicit(lockUrl, titleCs)))) ||
        (hostPeek &&
          typeof isAppealHardDenyHost === "function" &&
          isAppealHardDenyHost(hostPeek, polPeek)) ||
        (hostPeek && typeof isForcePornHost === "function" && isForcePornHost(hostPeek, polPeek));
      var scoreHard = scoreCs >= 20;

      // Quinn false-positive grant — pause content-score lock for the appeal window.
      if (
        hostPeek &&
        typeof isHostAppealAllowed === "function" &&
        typeof isAppealHardDenyHost === "function" &&
        !isAppealHardDenyHost(hostPeek, polPeek) &&
        isHostAppealAllowed(hostPeek, appealAllowsCache, Date.now())
      ) {
        sendResponse({ ok: true, skipped: true, reason: "appeal_allow" });
        return;
      }

      // SoftLand allow-list may open the host — keyword score still overrides.
      var native = null;
      try {
        native = await softlandNativeGetMode(lockUrl);
      } catch (eNat) {
        native = null;
      }
      var overAllow = Boolean(
        native &&
          (native.reason === "allow_list" ||
            (native.action === "allow" && String(native.reason || "").indexOf("allow") >= 0))
      );
      // Allow-listed hosts: lock from warn floor (keywords checked even after Allow site).
      var needScore = overAllow ? warnFloor : lockFloor;
      var adultLock = listHard || scoreHard || scoreCs >= needScore;

      if (!redirectsEnabled && !adultLock) {
        sendResponse({ ok: false, reason: "redirects_off" });
        return;
      }
      // Inspect: only soft (non-adult) content-score locks stay paused.
      if (inspectModeCached && !adultLock) {
        sendResponse({ ok: true, skipped: true, reason: "inspect" });
        return;
      }

      // Score below floor and not list-hard → do not lock.
      if (scoreCs < needScore && !listHard && !scoreHard) {
        sendResponse({ ok: true, skipped: true, reason: "below_lock", need: needScore });
        return;
      }
      var allowSoft =
        typeof allowHostContentLock === "function"
          ? allowHostContentLock({
              host: hostPeek,
              url: lockUrl,
              title: titleCs,
              excerpt: String(msg.excerpt || ""),
              matched: matchedCs,
              score: scoreCs,
            })
          : { skip: false };
      if (allowSoft.skip) {
        sendResponse({ ok: true, skipped: true, reason: allowSoft.why || "allow_host_soft" });
        return;
      }

      var searchPageCs =
        typeof webSearchQuery === "function" && webSearchQuery(lockUrl) !== null;
      var quinnSkip =
        (hostPeek &&
          typeof isAppealHardDenyHost === "function" &&
          isAppealHardDenyHost(hostPeek, polPeek)) ||
        (hostPeek && typeof isSensitiveHost === "function" && isSensitiveHost(hostPeek, polPeek)) ||
        (!searchPageCs &&
          typeof urlLooksExplicit === "function" &&
          urlLooksExplicit(lockUrl, titleCs));
      if (!quinnSkip && typeof askQuinnSentenceConfirm === "function") {
        var quinnVerdict = null;
        try {
          quinnVerdict = await askQuinnSentenceConfirm({
            host: hostPeek,
            url: lockUrl,
            title: titleCs,
            excerpt: String(msg.excerpt || ""),
            matched: matchedCs,
            score: scoreCs,
          });
        } catch (eQuinn) {
          quinnVerdict = null;
        }
        if (quinnVerdict && quinnVerdict.allow) {
          sendResponse({
            ok: true,
            skipped: true,
            reason: "quinn_finalize",
            why: quinnVerdict.why || "",
          });
          return;
        }
      }

      // Last stage: screenshot. A clean image cancels the block on search
      // and allow-listed hosts. Known tubes never reach this.
      var imageHost =
        hostPeek &&
        !(typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(hostPeek, polPeek)) &&
        !(typeof isForcePornHost === "function" && isForcePornHost(hostPeek, polPeek)) &&
        (searchPageCs ||
          (typeof isFallbackAllowHost === "function" && isFallbackAllowHost(hostPeek)));
      if (imageHost && typeof tabId === "number" && typeof askImageVerify === "function") {
        var seen = null;
        try {
          seen = await askImageVerify(tabId);
        } catch (eImg) {
          seen = null;
        }
        if (seen && seen.adult === false) {
          sendResponse({
            ok: true,
            skipped: true,
            reason: "image_clean",
            why: seen.why || "",
          });
          return;
        }
      }

      // Keyword locks stay "content_score" / "keyword" — not "porn" — unless host is a hard tube.
      var kindCs = "keyword";
      if (listHard) kindCs = "porn";
      else if (scoreHard || scoreCs >= lockFloor) kindCs = "keyword";
      reportGateAlert(
        kindCs === "porn" ? "porn" : "distraction",
        "content_score:" +
          scoreCs +
          (overAllow ? " over_allow_list" : "") +
          " " +
          lockUrl.slice(0, 100)
      );
      if (typeof tabId !== "number") {
        sendResponse({ ok: false, reason: "no_tab" });
        return;
      }
      var target = lockedPageUrlForBlocked(lockUrl, kindCs);
      var locked = await softLandBlockedTab(tabId, target, {
        fromUrl: lockUrl,
        kind: kindCs,
        host: hostPeek || (native && native.matched) || "",
      });
      if (!locked) locked = await forceNavigateToLock(tabId, target);
      sendResponse({
        ok: true,
        locked: Boolean(locked),
        score: scoreCs,
        over_allow_list: overAllow,
      });
    })();
    return true;
  }
  if (msg.type === "GET_GATE") {
    void syncQuinnFromFocus({ force: true }).then(function () {
      extAPI.storage.local.get(["caltInspectMode"], function (st) {
        sendResponse({
          gateCache: gateCache,
          redirectsEnabled: redirectsEnabled,
          nativeHostOk: nativeHostOk,
          studyHttpFallback: studyHttpFallback,
          inspectMode: Boolean(st && st.caltInspectMode),
          quinnEnabled: quinnEnabledCached !== false,
        });
      });
    });
    return true;
  }
  // Legacy: Quinn on/off is owned by Focus Home — refresh from mirror only.
  if (msg.type === "SET_QUINN_ENABLED") {
    void syncQuinnFromFocus({ force: true }).then(function () {
      sendResponse({ ok: true, quinnEnabled: quinnEnabledCached !== false, source: "focus" });
    });
    return true;
  }
  if (msg.type === "SET_INSPECT_MODE") {
    var onBg = Boolean(msg.enabled);
    inspectModeCached = onBg;
    extAPI.storage.local.set({ caltInspectMode: onBg }, function () {
      void syncDeclarativeWatchBlock({ force: true });
      void syncDeclarativePornBlock({ force: true });
      sendResponse({ ok: true, inspectMode: onBg });
    });
    return true;
  }
  if (msg.type === "INSPECT_GET_MODE") {
    void softlandNativeGetMode(String(msg.url || "")).then(function (native) {
      sendResponse({ ok: true, native: native || null, nativeHostOk: nativeHostOk });
    });
    return true;
  }
  if (msg.type === "REFRESH_GATE") {
    void pollGate({ force: true }).then(function () {
      return syncQuinnFromFocus({ force: true });
    }).then(function () {
      extAPI.storage.local.get(["caltInspectMode"], function (st) {
        sendResponse({
          ok: true,
          gateCache: gateCache,
          nativeHostOk: nativeHostOk,
          studyHttpFallback: studyHttpFallback,
          inspectMode: Boolean(st && st.caltInspectMode),
          quinnEnabled: quinnEnabledCached !== false,
        });
      });
    });
    return true;
  }
  if (msg.type === "SET_REDIRECTS") {
    redirectsEnabled = true;
    extAPI.storage.local.set({ redirectsEnabled: true });
    void syncDeclarativeWatchBlock({ force: true });
    void syncDeclarativePornBlock({ force: true });
    sendResponse({ ok: true, redirectsEnabled: true });
    return false;
  }
  if (msg.type === "TEMP_ALLOW_STATUS") {
    var hostS = String(msg.host || "")
      .toLowerCase()
      .replace(/^www\./, "");
    pruneAndLoadTempAllows(function (list) {
      var polS = typeof browserPolicyOrFallback === "function" ? browserPolicyOrFallback(gateCache && gateCache.browser) : {};
      var excludedS = !hostS || (typeof isTempAllowExcludedHost === "function" && isTempAllowExcludedHost(hostS, polS));
      var untilS =
        typeof tempAllowUntilForHost === "function" ? tempAllowUntilForHost(hostS, list, Date.now()) : 0;
      sendResponse({
        ok: true,
        host: hostS,
        excluded: excludedS,
        can_allow: Boolean(hostS) && !excludedS,
        allowed_until: untilS || 0,
        temp_allow_ms: typeof TEMP_ALLOW_MS !== "undefined" ? TEMP_ALLOW_MS : 60000,
        temp_allows: list,
      });
    });
    return true;
  }
  if (msg.type === "TEMP_ALLOW_REQUEST") {
    var hostR = String(msg.host || "")
      .toLowerCase()
      .replace(/^www\./, "");
    var nowR = Date.now();
    var polR = typeof browserPolicyOrFallback === "function" ? browserPolicyOrFallback(gateCache && gateCache.browser) : {};
    var grant = typeof buildTempAllowGrant === "function" ? buildTempAllowGrant(hostR, nowR, polR) : { ok: false };
    if (!grant.ok) {
      sendResponse({
        ok: false,
        error: (grant && grant.error) || "This site can't be temporarily allowed",
        host: hostR,
      });
      return false;
    }
    pruneAndLoadTempAllows(function (list) {
      list = typeof upsertTempAllow === "function" ? upsertTempAllow(list, grant.entry, nowR) : list.concat([grant.entry]);
      tempAllowsCache = list;
      var key = TEMP_ALLOW_STORAGE_KEY || "tempAllows";
      var payload = {};
      payload[key] = list;
      payload.tempAllows = list;
      extAPI.storage.local.set(payload, function () {
        invalidateGetModeCache(grant.entry.host);
        sendResponse({
          ok: true,
          host: grant.entry.host,
          until: grant.entry.until,
          temp_allow_ms: typeof TEMP_ALLOW_MS !== "undefined" ? TEMP_ALLOW_MS : 60000,
        });
      });
    });
    return true;
  }
  if (msg.type === "PORN_APPEAL_REQUEST") {
    void handlePornAppealRequest(msg).then(function (resp) {
      sendResponse(resp);
    });
    return true;
  }
  return false;
});

/** Search-results URL → the query is the use case. Otherwise the case is the page itself. */
function quinnWebSearchUseCase(url) {
  var u;
  try {
    u = new URL(String(url || ""));
  } catch (e) {
    return { useCase: "page", query: "" };
  }
  var h = u.hostname.toLowerCase().replace(/^www\./, "");
  var q =
    u.searchParams.get("q") ||
    u.searchParams.get("query") ||
    u.searchParams.get("p") ||
    u.searchParams.get("text") ||
    "";
  var searchHost =
    /(^|\.)google\./.test(h) ||
    h === "bing.com" ||
    h.endsWith(".bing.com") ||
    h === "duckduckgo.com" ||
    h.endsWith(".duckduckgo.com") ||
    h === "search.brave.com" ||
    h === "ecosia.org" ||
    h === "startpage.com" ||
    h === "kagi.com" ||
    h === "search.yahoo.com";
  var searchPath = u.pathname.indexOf("/search") >= 0 || h.indexOf("duckduckgo.") >= 0;
  if (searchHost && (searchPath || q)) {
    return { useCase: "web_search", query: String(q).replace(/\s+/g, " ").trim().slice(0, 300) };
  }
  return { useCase: "page", query: "" };
}

/** Headless read: extension fetch, no tab. Title plus visible text. */
async function quinnHeadlessContent(url) {
  var empty = { title: "", text: "" };
  var raw = String(url || "");
  if (raw.indexOf("http://") !== 0 && raw.indexOf("https://") !== 0) return empty;
  if (raw.indexOf("locked.html") >= 0) return empty;
  try {
    var res = await fetch(raw, {
      method: "GET",
      redirect: "follow",
      credentials: "omit",
      signal: AbortSignal.timeout(4000),
    });
    if (!res.ok) return empty;
    var ct = String(res.headers.get("content-type") || "");
    if (ct && ct.indexOf("text") < 0 && ct.indexOf("html") < 0 && ct.indexOf("json") < 0) return empty;
    var html = await res.text();
    if (!html) return empty;
    html = html.replace(/<script[\s\S]*?<\/script>/gi, " ").replace(/<style[\s\S]*?<\/style>/gi, " ");
    var title = "";
    var tm = html.match(/<title[^>]*>([\s\S]*?)<\/title>/i);
    if (tm) title = tm[1].replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim().slice(0, 200);
    var text = html
      .replace(/<[^>]+>/g, " ")
      .replace(/&nbsp;/gi, " ")
      .replace(/&amp;/gi, "&")
      .replace(/\s+/g, " ")
      .trim()
      .slice(0, 1400);
    return { title: title, text: text };
  } catch (e) {
    return empty;
  }
}

function quinnPlain(s) {
  return String(s || "")
    .replace(/<[^>]+>/g, " ")
    .replace(/&amp;/gi, "&")
    .replace(/&quot;/gi, '"')
    .replace(/&#39;|&apos;/gi, "'")
    .replace(/&lt;/gi, "<")
    .replace(/&gt;/gi, ">")
    .replace(/&nbsp;/gi, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function quinnParseSearchHits(html) {
  var src = String(html || "");
  var titles = [];
  var snippets = [];
  var m;
  var reT = /class="result__a"[^>]*>([\s\S]*?)<\/a>/gi;
  while ((m = reT.exec(src)) && titles.length < 4) {
    var t = quinnPlain(m[1]).slice(0, 140);
    if (t) titles.push(t);
  }
  var reS = /class="result__snippet"[^>]*>([\s\S]*?)<\/a>/gi;
  while ((m = reS.exec(src)) && snippets.length < 4) {
    var sn = quinnPlain(m[1]).slice(0, 220);
    if (sn) snippets.push(sn);
  }
  var out = [];
  var n = Math.max(titles.length, snippets.length);
  for (var i = 0; i < n && out.length < 4; i++) {
    var line = titles[i] || "";
    if (snippets[i]) line = line ? line + " — " + snippets[i] : snippets[i];
    if (line) out.push("- " + line);
  }
  return out.join("\n").slice(0, 900);
}

/** Headless web search: what this website is used for. No tab. */
async function quinnWebSearch(query) {
  var q = String(query || "")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 180);
  if (!q) return "";
  var signal = AbortSignal.timeout(4500);
  var abstractText = "";
  var hits = "";
  try {
    var pair = await Promise.all([
      fetch(
        "https://api.duckduckgo.com/?format=json&no_html=1&skip_disambig=1&q=" + encodeURIComponent(q),
        { credentials: "omit", signal: signal }
      )
        .then(function (r) {
          return r.ok ? r.json() : null;
        })
        .catch(function () {
          return null;
        }),
      fetch("https://html.duckduckgo.com/html/?q=" + encodeURIComponent(q), {
        credentials: "omit",
        signal: signal,
        headers: { Accept: "text/html" },
      })
        .then(function (r) {
          return r.ok ? r.text() : "";
        })
        .catch(function () {
          return "";
        }),
    ]);
    var j = pair[0];
    if (j && j.AbstractText) {
      abstractText = quinnPlain((j.Heading ? j.Heading + ": " : "") + j.AbstractText).slice(0, 500);
    }
    hits = quinnParseSearchHits(pair[1] || "");
  } catch (e) {
    return "";
  }
  if (abstractText && hits) return (abstractText + "\n" + hits).slice(0, 1100);
  return abstractText || hits || "";
}

/**
 * Evidence Quinn must read before it finalizes:
 * page context, a web search of what the website is used for, and the site name.
 */
async function gatherQuinnCase(payload) {
  payload = payload || {};
  var host = typeof normalizeHost === "function" ? normalizeHost(payload.host || "") : String(payload.host || "");
  var url = String(payload.url || "");
  var title = String(payload.title || "").slice(0, 200);
  var content = String(payload.excerpt || payload.content || "").slice(0, 1400);
  var search = quinnWebSearchUseCase(url);
  var websiteUsedFor = "";
  if (search.useCase === "web_search") {
    try {
      var qu = new URL(url);
      ["gs_lcrp", "cvid", "form", "pglt", "pc", "adppc", "qs", "sp", "pq", "sc", "sk"].forEach(
        function (k) {
          qu.searchParams.delete(k);
          qu.searchParams.delete(k.toUpperCase());
        }
      );
      url = qu.toString();
    } catch (eClean) {
      /* keep original url */
    }
  }
  var pending = [];
  if (content.length < 80 && url) {
    pending.push(
      quinnHeadlessContent(url).then(function (fetched) {
        if (!title && fetched.title) title = fetched.title;
        if (fetched.text && fetched.text.length > content.length) content = fetched.text;
      })
    );
  }
  if (host) {
    pending.push(
      quinnWebSearch("what is " + host + " used for").then(function (found) {
        websiteUsedFor = found || "";
      })
    );
  }
  if (pending.length) await Promise.all(pending);
  return {
    websiteName: host || "(unknown)",
    url: url.slice(0, 400),
    title: title,
    content: content,
    useCase: search.useCase,
    query: search.query,
    note: String(payload.note || "").slice(0, 400),
    websiteUsedFor: websiteUsedFor,
  };
}

function quinnEvidenceBlock(c) {
  c = c || {};
  return (
    "website_name: " +
    (c.websiteName || "(unknown)") +
    "\nwebsite_used_for:\n" +
    (c.websiteUsedFor || "(no web search result)") +
    "\nuse_case: " +
    (c.useCase || "page") +
    "\nsearch_query: " +
    (c.query || "(none)") +
    "\npage_title: " +
    (c.title || "(none)") +
    "\nuser_note: " +
    (c.note || "(none)") +
    "\ncontent:\n" +
    (c.content || "(none)")
  );
}

var visionModelCached = "";

function ollamaOrigin() {
  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "")
    .replace(/\/$/, "");
  return base.replace(/\/v1$/, "");
}

/** Prefer a local vision model. The text Quinn model cannot see a screenshot. */
async function resolveVisionModel() {
  if (visionModelCached) return visionModelCached;
  if (llmHostPrefs && llmHostPrefs.visionModel) {
    visionModelCached = String(llmHostPrefs.visionModel);
    return visionModelCached;
  }
  try {
    var res = await fetch(ollamaOrigin() + "/api/tags", { signal: AbortSignal.timeout(2500) });
    if (!res.ok) return "";
    var j = await res.json();
    var models = (j && j.models) || [];
    var i;
    for (i = 0; i < models.length; i++) {
      var name = String((models[i] && (models[i].name || models[i].model)) || "");
      if (/vl|llava|moondream|minicpm-v|bakllava|vision/i.test(name)) {
        visionModelCached = name;
        return name;
      }
    }
  } catch (e) {
    return "";
  }
  return "";
}

function captureVisibleJpeg(tabId) {
  return new Promise(function (resolve) {
    if (!extAPI.tabs || !extAPI.tabs.get || !extAPI.tabs.captureVisibleTab) {
      resolve("");
      return;
    }
    extAPI.tabs.get(tabId, function (tab) {
      if (extAPI.runtime && extAPI.runtime.lastError) {
        resolve("");
        return;
      }
      if (!tab || !tab.active || tab.windowId == null) {
        resolve("");
        return;
      }
      var shot = extAPI.tabs.captureVisibleTab(tab.windowId, { format: "jpeg", quality: 28 });
      if (shot && typeof shot.then === "function") {
        shot.then(function (url) { resolve(url || ""); }).catch(function () { resolve(""); });
      } else {
        resolve("");
      }
    });
  });
}

/**
 * Last block stage. adult:false means the screenshot is not porn, so the
 * text score must not lock the tab. adult:null means no vision model or no shot.
 */
async function askImageVerify(tabId) {
  var dataUrl = await captureVisibleJpeg(tabId);
  if (!dataUrl || String(dataUrl).indexOf("data:image") !== 0) {
    return { adult: null, why: "no screenshot" };
  }
  var model = await resolveVisionModel();
  if (!model) return { adult: null, why: "no vision model" };
  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "")
    .replace(/\/$/, "");
  var endpoint = base + "/chat/completions";
  var headers = { "Content-Type": "application/json" };
  if (llmHostPrefs && llmHostPrefs.apiKey) headers.Authorization = "Bearer " + llmHostPrefs.apiKey;
  try {
    var res = await fetch(endpoint, {
      method: "POST",
      headers: headers,
      body: JSON.stringify({
        model: model,
        temperature: 0.05,
        messages: [
          {
            role: "system",
            content:
              "You are Quinn, the last stage before a CALT block. Look at the browser screenshot. " +
              "adult true only for porn, nudity, a sex act, a cam, or an explicit gallery. " +
              "A search page, news page, code, or docs page is adult false even if an adult word is visible. " +
              'Reply ONLY JSON: {"adult":true|false,"why":"what the image shows"}.',
          },
          {
            role: "user",
            content: [
              { type: "text", text: "Is this screenshot adult content that should be blocked?" },
              { type: "image_url", image_url: { url: dataUrl } },
            ],
          },
        ],
      }),
      signal: AbortSignal.timeout(20000),
    });
    if (!res.ok) return { adult: null, why: "vision HTTP " + res.status };
    var j = await res.json();
    var text = String((((j.choices || [])[0] || {}).message || {}).content || "").trim();
    var brace = text.indexOf("{");
    var end = text.lastIndexOf("}");
    if (brace >= 0 && end > brace) text = text.slice(brace, end + 1);
    var parsed = JSON.parse(text);
    if (typeof parsed.adult !== "boolean") return { adult: null, why: "vision parse" };
    return { adult: parsed.adult, why: String(parsed.why || "").slice(0, 200), model: model };
  } catch (e) {
    return { adult: null, why: "vision unreachable" };
  }
}

/**
 * Pre-lock sentence confirmation after keyword content_score.
 * Distinguishes "how to block adult content" chats from actual porn pages.
 * Fail-closed on errors / low confidence.
 */
async function askQuinnSentenceConfirm(payload) {
  var host = normalizeHost(payload.host || "");
  var url = String(payload.url || "");
  var title = String(payload.title || "").slice(0, 200);
  var excerpt = String(payload.excerpt || "").slice(0, 1400);
  var matched = Array.isArray(payload.matched) ? payload.matched.slice(0, 10) : [];
  var score = Number(payload.score) || 0;

  await syncQuinnFromFocus({ force: false });
  if (quinnEnabledCached === false) {
    return { allow: false, why: "Quinn OFF", error: "quinn_off" };
  }
  var polS =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  if (typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, polS)) {
    return { allow: false, why: "Known adult host", hard_deny: true, confidence: 1 };
  }
  if (typeof isSensitiveHost === "function" && isSensitiveHost(host, polS)) {
    return { allow: false, why: "Sensitive host", hard_deny: true, confidence: 1 };
  }

  var evidence = await gatherQuinnCase({
    host: host,
    url: url,
    title: title,
    excerpt: excerpt,
    note: "",
  });

  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "").replace(
    /\/$/,
    ""
  );
  var model = String((llmHostPrefs && llmHostPrefs.model) || QWEN_DEFAULT_MODEL || "qwen2.5:3b");
  var endpoint = base + "/chat/completions";

  var system =
    "You are Quinn, CALT sentence confirmer. Evidence is already collected headlessly. " +
    "Read website_name, website_used_for (a web search of what that site is for), content, and use_case, then finalize. " +
    "The why sentence must say what the website is used for. " +
    "Allow a code host, encyclopedia, docs site, news site, or a web search whose query is research or how to block adult content. " +
    "Block porn media, a tube, a cam site, or a search that asks for porn or hentai. " +
    'Reply ONLY JSON: {"allow":true|false,"confidence":0.0-1.0,"why":"what the site is for, then the decision"}. ' +
    "Default allow:false. Never allow tubes, hentai, cams, OnlyFans-style hosts, or erotic galleries.";

  var user =
    quinnEvidenceBlock(evidence) +
    "\nurl: " +
    url.slice(0, 400) +
    "\nkeyword_score: " +
    score +
    "\nmatched_keywords: " +
    matched.join(", ");

  try {
    var headers = { "Content-Type": "application/json" };
    if (llmHostPrefs && llmHostPrefs.apiKey) {
      headers.Authorization = "Bearer " + llmHostPrefs.apiKey;
    }
    var res = await fetch(endpoint, {
      method: "POST",
      headers: headers,
      body: JSON.stringify({
        model: model,
        temperature: 0.05,
        messages: [
          { role: "system", content: system },
          { role: "user", content: user },
        ],
      }),
      signal: AbortSignal.timeout(12000),
    });
    if (!res.ok) {
      return { allow: false, why: "Quinn HTTP " + res.status, error: "llm_http" };
    }
    var j = await res.json();
    var text = (((j.choices || [])[0] || {}).message || {}).content || "";
    text = String(text).trim();
    var jsonStr = text;
    var brace = text.indexOf("{");
    var end = text.lastIndexOf("}");
    if (brace >= 0 && end > brace) jsonStr = text.slice(brace, end + 1);
    var parsed = {};
    try {
      parsed = JSON.parse(jsonStr);
    } catch (eParse) {
      return { allow: false, why: "Quinn parse fail — lock", error: "llm_parse" };
    }
    var allow = Boolean(parsed.allow);
    var confidence = Number(parsed.confidence);
    if (!(confidence >= 0 && confidence <= 1)) confidence = allow ? 0.5 : 0.8;
    if (allow && confidence < 0.7) {
      return {
        allow: false,
        why: "Low sentence-confirm confidence — lock",
        confidence: confidence,
      };
    }
    return {
      allow: allow,
      why: String(parsed.why || (allow ? "Advice context." : "Adult context.")).slice(0, 240),
      confidence: confidence,
      model: model,
      source: "sentence_quinn",
    };
  } catch (e) {
    return {
      allow: false,
      why: "Quinn timeout/unreachable — lock",
      error: e && e.message ? e.message : "unreachable",
    };
  }
}

/**
 * Ask local Qwen (Ollama) whether a SoftLand porn / content_score block looks like a
 * false positive. Hard-deny known adult hosts without calling the model.
 */
async function askQuinnAppeal(payload) {
  var host = normalizeHost(payload.host || "");
  var url = String(payload.url || "");
  var reason = String(payload.reason || "");
  var note = String(payload.note || "").slice(0, 400);
  var title = String(payload.title || "").slice(0, 200);
  var excerpt = String(payload.excerpt || "").slice(0, 1200);
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};

  if (!host) return { allow: false, why: "No host to review.", hard_deny: true };
  await syncQuinnFromFocus({ force: true });
  if (quinnEnabledCached === false) {
    return {
      allow: false,
      why: "Quinn is OFF - turn on in Focus Home (Quinn chip).",
      error: "quinn_off",
    };
  }
  if (typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, pol)) {
    return {
      allow: false,
      why: "Known adult / porn host — Quinn will not override this block.",
      hard_deny: true,
      confidence: 1,
    };
  }
  if (typeof isSensitiveHost === "function" && isSensitiveHost(host, pol)) {
    return {
      allow: false,
      why: "Sensitive host — Quinn will not open this.",
      hard_deny: true,
      confidence: 1,
    };
  }

  var researchHost =
    typeof isFallbackAllowHost === "function" &&
    isFallbackAllowHost(host) &&
    !(typeof isForcePornHost === "function" && isForcePornHost(host, pol));
  var searchQuery =
    typeof webSearchQuery === "function" ? webSearchQuery(url) : null;
  var searchPage = searchQuery !== null;
  var explicitAppeal =
    typeof urlLooksExplicit === "function" && urlLooksExplicit(url, title);
  var adultAppeal =
    typeof urlLooksAdult === "function" && urlLooksAdult(url, title);

  // Stage-0 appeal: allow-listed hosts (Claude, Bing, GitHub, …) are not tubes.
  // softland_reason=porn only means the auto-lock fired — often a false positive.
  if (researchHost && !explicitAppeal && !adultAppeal) {
    var openWhy = searchPage
      ? "Search engine — query is not adult (" +
        String(searchQuery || "").slice(0, 80) +
        "). Opened for this appeal."
      : "Allow-listed host (" +
        host +
        ") — not a tube. Opened for this appeal.";
    return {
      allow: true,
      why: openWhy,
      confidence: 0.96,
      source: searchPage ? "search_query_allow" : "allow_list_appeal",
    };
  }

  var evidence = await gatherQuinnCase({
    host: host,
    url: url,
    title: title,
    excerpt: excerpt,
    note: note,
  });

  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "")
    .replace(/\/$/, "");
  var model = String((llmHostPrefs && llmHostPrefs.model) || QWEN_DEFAULT_MODEL || "qwen2.5:3b");
  var endpoint = base + "/chat/completions";

  var system =
    "You are Quinn, CALT Focus appeal reviewer. Evidence is already collected headlessly. " +
    "Read website_name, website_used_for, use_case, search_query, content, and the user note, then finalize. " +
    "The why sentence must say what the website is used for. " +
    "lock_trigger is ONLY why SoftLand auto-locked — it is often a false positive. " +
    "Never treat lock_trigger=porn or the word porn in a user note about blocking porn as proof the site is a tube. " +
    "Claude, ChatGPT, Gemini, GitHub, docs, and search engines are tools — allow:true unless the URL itself asks for porn. " +
    "If the user note says they are building a porn blocker or asking for help, allow:true. " +
    "Block only porn media, tubes, cams, or a search that asks for porn or hentai. " +
    "allow must match why. Reply with ONLY compact JSON: " +
    '{"allow":true|false,"confidence":0.0-1.0,"why":"what the site is for, then the decision"}. ' +
    "Known tubes never reach you.";

  var user =
    quinnEvidenceBlock(evidence) +
    "\nurl: " +
    evidence.url +
    "\nlock_trigger: " +
    reason +
    "\nlock_trigger_note: automatic SoftLand label — not ground truth that the site is porn";

  try {
    var headers = { "Content-Type": "application/json" };
    if (llmHostPrefs && llmHostPrefs.apiKey) {
      headers.Authorization = "Bearer " + llmHostPrefs.apiKey;
    }
    var res = await fetch(endpoint, {
      method: "POST",
      headers: headers,
      body: JSON.stringify({
        model: model,
        temperature: 0.1,
        messages: [
          { role: "system", content: system },
          { role: "user", content: user },
        ],
      }),
      signal: AbortSignal.timeout(35000),
    });
    if (!res.ok) {
      return { allow: false, why: "Quinn unreachable (HTTP " + res.status + ").", error: "llm_http" };
    }
    var j = await res.json();
    var text = (((j.choices || [])[0] || {}).message || {}).content || "";
    text = String(text).trim();
    var jsonStr = text;
    var brace = text.indexOf("{");
    var end = text.lastIndexOf("}");
    if (brace >= 0 && end > brace) jsonStr = text.slice(brace, end + 1);
    var parsed = {};
    try {
      parsed = JSON.parse(jsonStr);
    } catch (eParse) {
      return { allow: false, why: "Quinn reply was not valid JSON — keeping block.", error: "llm_parse", raw: text.slice(0, 200) };
    }
    var allow = Boolean(parsed.allow);
    var confidence = Number(parsed.confidence);
    if (!(confidence >= 0 && confidence <= 1)) confidence = allow ? 0.5 : 0.8;
    var whyText = String(parsed.why || "").slice(0, 240);
    // Require clear confidence to open a blocked site.
    if (allow && confidence < 0.65) {
      return {
        allow: false,
        why: (whyText || "Low confidence") + " (confidence " + confidence + " — keeping block).",
        confidence: confidence,
        model: model,
      };
    }
    var slogan =
      /always blocked|no quinn override|no temp allow|will not override/i.test(whyText);
    var genuineWhy =
      /\b(search engine|web search|does not host porn|does not (host|contain) (any )?(porn|hentai|adult)|primarily used for web search|encyclopedia|documentation|code host)\b/i.test(
        whyText
      ) &&
      !/\b(hosts? porn|is (a )?(porn|tube|cam)|adult (media|gallery)|block(ed|ing)? (this|the) (site|page))\b/i.test(
        whyText
      );
    if (
      !allow &&
      researchHost &&
      !explicitAppeal &&
      !adultAppeal &&
      (slogan ||
        genuineWhy ||
        (evidence && evidence.useCase === "web_search") ||
        (typeof noteLooksResearch === "function" && noteLooksResearch(note)))
    ) {
      return {
        allow: true,
        why: whyText || "Research / search host — opened for this appeal. Known tubes stay blocked.",
        confidence: Math.max(confidence, 0.9),
        model: model,
        source: "appeal_override",
      };
    }
    return {
      allow: allow,
      why: whyText || (allow ? "Looks genuine." : "Looks like adult content."),
      confidence: confidence,
      model: model,
    };
  } catch (e) {
    return {
      allow: false,
      why: "Quinn unreachable — start Ollama (qwen2.5:3b) in Focus Settings → LLM host.",
      error: e && e.message ? e.message : "unreachable",
    };
  }
}

async function handlePornAppealRequest(msg) {
  var host = normalizeHost(msg.host || "");
  var url = String(msg.url || "");
  var reason = String(msg.reason || "porn");
  var note = String(msg.note || "");
  var title = String(msg.title || "");
  var excerpt = String(msg.excerpt || "");
  if (!host) return { ok: false, error: "No host", allow: false };

  var verdict = await askQuinnAppeal({
    host: host,
    url: url,
    reason: reason,
    note: note,
    title: title,
    excerpt: excerpt,
  });

  try {
    await nativeCall(
      {
        type: "log_appeal",
        host: host,
        url: url.slice(0, 400),
        reason: reason,
        allow: Boolean(verdict && verdict.allow),
        why: String((verdict && verdict.why) || "").slice(0, 240),
        confidence: Number((verdict && verdict.confidence) || 0),
        note: note.slice(0, 200),
      },
      800
    );
  } catch (eLog) {
    /* optional */
  }

  if (!verdict || !verdict.allow) {
    return {
      ok: true,
      allow: false,
      host: host,
      why: (verdict && verdict.why) || "Quinn kept the block.",
      confidence: verdict && verdict.confidence,
      hard_deny: Boolean(verdict && verdict.hard_deny),
      error: verdict && verdict.error,
    };
  }

  var nowA = Date.now();
  var untilA =
    nowA + (typeof APPEAL_ALLOW_MS === "number" ? APPEAL_ALLOW_MS : 10 * 60 * 1000);
  var entry = {
    host: host,
    until: untilA,
    why: String(verdict.why || "Quinn false-positive grant").slice(0, 240),
    by: "qwen",
  };
  appealAllowsCache =
    typeof upsertAppealAllow === "function"
      ? upsertAppealAllow(appealAllowsCache, entry, nowA)
      : (appealAllowsCache || []).concat([entry]);
  var keyA = APPEAL_ALLOW_STORAGE_KEY || "appealAllows";
  var payloadA = {};
  payloadA[keyA] = appealAllowsCache;
  payloadA.appealAllows = appealAllowsCache;
  try {
    await new Promise(function (resolve) {
      extAPI.storage.local.set(payloadA, resolve);
    });
  } catch (eSet) {
    /* ignore */
  }
  try {
    invalidateGetModeCache(host);
  } catch (eInv) {
    /* ignore */
  }
  // Refresh DNR fingerprint so appeal state is tracked (force tubes stay in DNR).
  try {
    void syncDeclarativePornBlock({ force: true });
  } catch (eDnr) {
    /* ignore */
  }

  return {
    ok: true,
    allow: true,
    host: host,
    until: untilA,
    why: entry.why,
    confidence: verdict.confidence,
    model: verdict.model,
  };
}
