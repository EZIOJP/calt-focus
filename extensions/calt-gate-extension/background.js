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
   applyContentScoreFromPolicy, CALT_BIBLE_URL, CALT_PRODUCTIVITY_URL, isFreeDay */

// Shared helpers are prepended by scripts/build_extension_workers.ps1 into service_worker.js

var DNR_WATCH_RULE_BASE = 9200;
/** Cap DNR rules — SoftLand covers any overflow from server watch_domains. */
var MAX_DNR_WATCH_HOSTS = 16;
/** Hard-block distraction hosts in every mode (including FREE). */
var DNR_PORN_RULE_BASE = 9300;
var MAX_DNR_PORN_HOSTS = 40;
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
var GET_MODE_TTL_MS = 2500;
var getModeCache = Object.create(null);
var nativePort = null;
var nativeQueue = [];
var nativeInFlight = false;
var gateBootstrapped = false;

extAPI.storage.local.get(
  ["gateCache", "redirectsEnabled", "tempAllows", "caltGateHttpFallback", "caltSoftlandHttpFallback"],
  function (result) {
    if (result.gateCache) gateCache = result.gateCache;
    if (typeof result.redirectsEnabled === "boolean") redirectsEnabled = result.redirectsEnabled;
    if (Array.isArray(result.tempAllows)) {
      tempAllowsCache =
        typeof pruneTempAllows === "function" ? pruneTempAllows(result.tempAllows, Date.now()) : result.tempAllows;
    }
    studyHttpFallback = Boolean(result.caltGateHttpFallback || result.caltSoftlandHttpFallback);
  }
);

function lockedPageUrl() {
  return extAPI.runtime.getURL("locked.html");
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
      nativePort.onDisconnect.addListener(function () {
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
    extAPI.notifications.create("calt-gate-" + now, {
      type: "basic",
      iconUrl: "icon.png",
      title: title || "CALT blocked a webpage",
      message: message || "This site is SoftLand-blocked. Edge stayed open.",
      priority: 2,
      requireInteraction: false,
    });
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
  var active = watchBlockActive();
  var fingerprint = (active ? "1" : "0") + "|" + hosts.join(",");
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
  var hosts =
    typeof dnrHostList === "function"
      ? dnrHostList(pol.force_porn_hosts || pol.porn_domains, pol.porn_domains, MAX_DNR_PORN_HOSTS)
      : (pol.porn_domains || FORCE_PORN_HOSTS || []).slice(0, MAX_DNR_PORN_HOSTS);
  var active = redirectsEnabled !== false;
  var fingerprint = (active ? "1" : "0") + "|porn|" + hosts.join(",");
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
  try {
    var native = await softlandNativeGetDoc();
    if (native && native.ok !== false && (native.browser || native.enabled !== undefined)) {
      await applyGatePayload(native, { force: opts.force, source: "native" });
      return;
    }

    // Study HTTP distraction-gate is opt-in debug only (caltGateHttpFallback / caltSoftlandHttpFallback).
    var st = await extAPI.storage.local.get(["caltGateHttpFallback", "caltSoftlandHttpFallback"]);
    studyHttpFallback = Boolean(st && (st.caltGateHttpFallback || st.caltSoftlandHttpFallback));
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
  if (tabId == null || !redirectsEnabled || redirectsPausedByCircuit()) return false;
  meta = meta || {};
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
    if (eu.indexOf("locked.html") >= 0) {
      if (!morningSoft || String(target).indexOf("locked.html") >= 0) {
        softLandDone[tabId] = { target: target, at: Date.now() };
        return false;
      }
    }
    if (eu && typeof isCaltSpaUrl === "function" && isCaltSpaUrl(eu) && morningSoft) {
      softLandDone[tabId] = { target: target, at: Date.now() };
      return false;
    }
    if (noteSoftLandAttempt()) return false;
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

async function maybeRedirectTab(tabId, url, title) {
  if (!redirectsEnabled) return false;
  if (!url || (typeof isExtensionOrInternalUrl === "function" && isExtensionOrInternalUrl(url))) return false;
  if (url.indexOf("locked.html") >= 0) return false;
  if (typeof isCaltSpaUrl === "function" && isCaltSpaUrl(url)) return false;

  var host = "";
  try {
    host = typeof hostnameFromUrl === "function" ? hostnameFromUrl(url) : "";
  } catch (e) {
    host = "";
  }
  if (host && hostCoveredByActiveDnr(host)) {
    return false;
  }

  // Extension-local 60s temp allow (C++ SoftLand has no temp-allow list).
  if (host && typeof isHostTempAllowed === "function") {
    var pol =
      typeof browserPolicyOrFallback === "function"
        ? browserPolicyOrFallback(gateCache && gateCache.browser)
        : {};
    if (isHostTempAllowed(host, tempAllowsCache, Date.now(), pol)) {
      return false;
    }
  }

  // Native SoftLand only — if msg_host fails, no SoftLand redirect (DNR still covers lists).
  var native = await softlandNativeGetMode(url);
  if (native && native.ok !== false && native.action) {
    if (native.action === "allow" || native.action === "none") return false;
    if (native.action === "block" && native.enforce !== false) {
      var kindN = native.reason || "blocked";
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
  if (msg.type === "CONTENT_SCORE_WARN") {
    return false;
  }
  if (msg.type === "CONTENT_SCORE_LOCK") {
    if (!redirectsEnabled) {
      sendResponse({ ok: false });
      return false;
    }
    var lockUrl = String(msg.url || "");
    var tabId = sender && sender.tab && sender.tab.id;
    void (async function () {
      var native = await softlandNativeGetMode(lockUrl);
      // Align with navigation: no native answer → do not SoftLand from content score alone.
      if (!native || native.ok === false) return;
      if (!native.softland_enabled || native.action === "allow" || native.mode === "free") {
        return;
      }
      if (native.action !== "block" || native.enforce === false) return;
      var hostCs = "";
      try {
        hostCs = typeof hostnameFromUrl === "function" ? hostnameFromUrl(lockUrl) : "";
      } catch (eCs) {
        hostCs = "";
      }
      if (hostCs && typeof isHostTempAllowed === "function") {
        var polCs =
          typeof browserPolicyOrFallback === "function"
            ? browserPolicyOrFallback(gateCache && gateCache.browser)
            : {};
        if (isHostTempAllowed(hostCs, tempAllowsCache, Date.now(), polCs)) return;
      }
      reportGateAlert(
        "distraction",
        "content_score:" + String(msg.score || 0) + " " + lockUrl.slice(0, 100)
      );
      if (typeof tabId === "number") {
        void softLandBlockedTab(tabId, lockedPageUrlForBlocked(lockUrl), {
          fromUrl: lockUrl,
          kind: "content_score",
        });
      }
    })();
    sendResponse({ ok: true });
    return false;
  }
  if (msg.type === "GET_GATE") {
    sendResponse({
      gateCache: gateCache,
      redirectsEnabled: redirectsEnabled,
      nativeHostOk: nativeHostOk,
      studyHttpFallback: studyHttpFallback,
    });
    return false;
  }
  if (msg.type === "REFRESH_GATE") {
    void pollGate({ force: true }).then(function () {
      sendResponse({
        ok: true,
        gateCache: gateCache,
        nativeHostOk: nativeHostOk,
        studyHttpFallback: studyHttpFallback,
      });
    });
    return true;
  }
  if (msg.type === "SET_REDIRECTS") {
    redirectsEnabled = Boolean(msg.enabled);
    extAPI.storage.local.set({ redirectsEnabled: redirectsEnabled });
    void syncDeclarativeWatchBlock({ force: true });
    void syncDeclarativePornBlock({ force: true });
    sendResponse({ ok: true, redirectsEnabled: redirectsEnabled });
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
  return false;
});
