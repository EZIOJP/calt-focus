/**
 * Light browser telemetry → native track_tab (required) + optional Python HTTP.
 * Cadence ~45s (idle backoff ~2 min). Domain-preferring; strips sensitive query tokens.
 * Edge-only SelfTracker telemetry (Chromium MV3).
 *
 * Phase 4 (Focus Standalone): HTTP POST /api/behavior/browser-telemetry is OPT-IN only
 * (chrome.storage.local caltPythonTelemetry=true). Default OFF. Required path is
 * native msg_host track_tab — works with Study :8000 stopped. Do not re-enable HTTP
 * by default.
 */
/* eslint-disable no-unused-vars */
/* global extAPI — set by background.js */

var TELEMETRY_URL = "http://127.0.0.1:8000/api/behavior/browser-telemetry";
/** Storage flag: enable Python HTTP browser-telemetry (default false). */
var PYTHON_TELEMETRY_STORAGE_KEY = "caltPythonTelemetry";
/** Active / focus cadence (ms). */
var TELEMETRY_CADENCE_MS = 90000;
/** When browser idle or locked. */
var TELEMETRY_IDLE_BACKOFF_MS = 180000;
/** Max open-tab URLs/domains sampled. */
var TELEMETRY_TOP_TABS = 5;
/** Recent history visit sample size (requires history permission). */
var TELEMETRY_HISTORY_N = 6;
/** If true, never send full paths — hostname only. */
var TELEMETRY_DOMAIN_ONLY = false;

var _telemetryLastPost = 0;
var _telemetryIdle = false;
var _telemetryTimer = null;

var _SENSITIVE_QUERY_RE =
  /(?:^|[&?])(token|access_token|auth|password|passwd|secret|api[_-]?key|session|sid|jwt|code|refresh[_-]?token)=[^&]*/gi;

function telemetrySanitizeUrl(url) {
  if (!url || typeof url !== "string") return "";
  if (/^(chrome|edge|about|moz-extension|chrome-extension|devtools):/i.test(url)) {
    return "";
  }
  try {
    var u = new URL(url);
    if (TELEMETRY_DOMAIN_ONLY) {
      return u.hostname.replace(/^www\./, "");
    }
    u.hash = "";
    var q = u.search || "";
    if (q) {
      q = q.replace(_SENSITIVE_QUERY_RE, "");
      // Drop empty leftovers
      q = q.replace(/[?&]+$/, "").replace(/\?&/, "?").replace(/^&/, "?");
      if (q === "?" || q === "") u.search = "";
      else u.search = q.startsWith("?") ? q : q ? "?" + q.replace(/^\?/, "") : "";
    }
    // Cap length
    var out = u.toString();
    return out.length > 400 ? out.slice(0, 400) : out;
  } catch (e) {
    return "";
  }
}

function telemetryDomain(url) {
  try {
    return new URL(url).hostname.replace(/^www\./, "");
  } catch (e) {
    return "";
  }
}

function telemetryBrowserLabel() {
  try {
    var ua = (typeof navigator !== "undefined" && navigator.userAgent) || "";
    if (/Edg\//i.test(ua)) return "edge";
    if (/Firefox|Zen/i.test(ua)) return "firefox";
    if (/Chrome\//i.test(ua)) return "chrome";
  } catch (e) {
    /* ignore */
  }
  return "chromium";
}

/**
 * @param {object} api chrome or browser
 * @param {object|null} gateCache
 * @returns {Promise<object|null>}
 */
async function buildBrowserTelemetryPayload(api, gateCache) {
  if (!api || !api.tabs) return null;
  var activeUrl = "";
  var activeTitle = "";
  var tabCount = 0;
  var openTabs = [];
  try {
    var all = await api.tabs.query({});
    tabCount = all.length;
    var scored = [];
    for (var i = 0; i < all.length; i++) {
      var t = all[i];
      if (!t || !t.url) continue;
      var clean = telemetrySanitizeUrl(t.url);
      if (!clean) continue;
      var domain = telemetryDomain(t.url) || clean;
      scored.push({
        url: TELEMETRY_DOMAIN_ONLY ? domain : clean,
        domain: domain,
        title: (t.title || "").slice(0, 120),
        active: Boolean(t.active),
        pinned: Boolean(t.pinned),
      });
      if (t.active) {
        activeUrl = TELEMETRY_DOMAIN_ONLY ? domain : clean;
        activeTitle = (t.title || "").slice(0, 160);
      }
    }
    // Prefer active + pinned, then first N
    scored.sort(function (a, b) {
      return (b.active ? 2 : 0) + (b.pinned ? 1 : 0) - ((a.active ? 2 : 0) + (a.pinned ? 1 : 0));
    });
    openTabs = scored.slice(0, TELEMETRY_TOP_TABS).map(function (row) {
      return TELEMETRY_DOMAIN_ONLY
        ? { domain: row.domain, active: row.active }
        : { url: row.url, domain: row.domain, title: row.title, active: row.active };
    });
  } catch (e) {
    return null;
  }

  var recentHistory = [];
  if (api.history && typeof api.history.search === "function") {
    try {
      var items = await new Promise(function (resolve) {
        try {
          var req = api.history.search({
            text: "",
            maxResults: TELEMETRY_HISTORY_N,
            startTime: Date.now() - 24 * 60 * 60 * 1000,
          });
          if (req && typeof req.then === "function") {
            req.then(resolve).catch(function () {
              resolve([]);
            });
          } else {
            api.history.search(
              {
                text: "",
                maxResults: TELEMETRY_HISTORY_N,
                startTime: Date.now() - 24 * 60 * 60 * 1000,
              },
              function (res) {
                resolve(res || []);
              },
            );
          }
        } catch (err) {
          resolve([]);
        }
      });
      for (var h = 0; h < (items || []).length && recentHistory.length < TELEMETRY_HISTORY_N; h++) {
        var hit = items[h];
        if (!hit || !hit.url) continue;
        var d = telemetryDomain(hit.url);
        if (!d) continue;
        recentHistory.push({
          domain: d,
          title: (hit.title || "").slice(0, 80),
          lastVisitTime: hit.lastVisitTime || null,
        });
      }
    } catch (e2) {
      /* history optional */
    }
  }

  return {
    source: "extension",
    browser: telemetryBrowserLabel(),
    domain_only: Boolean(TELEMETRY_DOMAIN_ONLY),
    active: {
      url: activeUrl || null,
      title: activeTitle || null,
      domain: activeUrl ? telemetryDomain(activeUrl) || activeUrl : null,
    },
    tab_count: tabCount,
    open_tabs: openTabs,
    recent_history: recentHistory,
    gate_locked: Boolean(gateCache && gateCache.locked),
    gate_enforce: Boolean(gateCache && gateCache.enforce),
    ts: Date.now(),
  };
}

/**
 * @param {object} api
 * @param {object|null} gateCache
 * @param {{force?: boolean}} opts
 */
async function maybePostBrowserTelemetry(api, gateCache, opts) {
  opts = opts || {};
  var now = Date.now();
  var gap = _telemetryIdle ? TELEMETRY_IDLE_BACKOFF_MS : TELEMETRY_CADENCE_MS;
  if (!opts.force && _telemetryLastPost && now - _telemetryLastPost < gap) {
    return;
  }
  try {
    var payload = await buildBrowserTelemetryPayload(api, gateCache);
    if (!payload) return;
    _telemetryLastPost = now;

    // Solo-pack: prefer native msg_host track_tab (works with :8000 stopped).
    var nativeOk = await new Promise(function (resolve) {
      try {
        if (!api.runtime || typeof api.runtime.connectNative !== "function") {
          resolve(false);
          return;
        }
        var port = api.runtime.connectNative("com.calt.msg_host");
        var done = false;
        var timer = setTimeout(function () {
          if (done) return;
          done = true;
          try {
            port.disconnect();
          } catch (e) {}
          resolve(false);
        }, 900);
        port.onMessage.addListener(function (msg) {
          if (done) return;
          done = true;
          clearTimeout(timer);
          try {
            port.disconnect();
          } catch (e2) {}
          resolve(Boolean(msg && msg.ok));
        });
        port.onDisconnect.addListener(function () {
          if (done) return;
          done = true;
          clearTimeout(timer);
          resolve(false);
        });
        var active = payload.active || {};
        port.postMessage({
          type: "track_tab",
          schema_version: 1,
          url: active.url || "",
          title: active.title || "",
          domain: active.domain || "",
          focused: true,
          ts: payload.ts || Date.now(),
        });
      } catch (e) {
        resolve(false);
      }
    });

    if (nativeOk) return;

    // Opt-in HTTP fallback only (default OFF). track_tab above is the required path.
    var httpEnabled = await new Promise(function (resolve) {
      try {
        if (!api.storage || !api.storage.local) {
          resolve(false);
          return;
        }
        api.storage.local.get([PYTHON_TELEMETRY_STORAGE_KEY], function (st) {
          resolve(!!(st && st[PYTHON_TELEMETRY_STORAGE_KEY] === true));
        });
      } catch (e2) {
        resolve(false);
      }
    });
    if (!httpEnabled) return;

    fetch(TELEMETRY_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
      cache: "no-store",
    }).catch(function () {});
  } catch (e) {
    /* ignore */
  }
}

function startBrowserTelemetry(api, getGateCache) {
  if (_telemetryTimer) return;
  function tick() {
    var g = typeof getGateCache === "function" ? getGateCache() : null;
    void maybePostBrowserTelemetry(api, g, {});
  }
  _telemetryTimer = setInterval(tick, 15000);
  // First snapshot soon after load
  setTimeout(tick, 4000);

  if (api.idle && api.idle.onStateChanged) {
    api.idle.onStateChanged.addListener(function (state) {
      _telemetryIdle = state === "idle" || state === "locked";
      if (state === "active") {
        void maybePostBrowserTelemetry(api, typeof getGateCache === "function" ? getGateCache() : null, {});
      }
    });
  }

  if (api.tabs && api.tabs.onActivated) {
    api.tabs.onActivated.addListener(function () {
      void maybePostBrowserTelemetry(api, typeof getGateCache === "function" ? getGateCache() : null, {});
    });
  }
}
