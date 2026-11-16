(function () {
  "use strict";
  if (window.__caltContentScoreArmed) return;
  window.__caltContentScoreArmed = true;

  var SAMPLE_MS = 2500;
  var TEXT_CAP = 6000;
  var LINK_CAP = 2000;
  var CHURN_CHARS = 800;
  var CHURN_NODES = 40;
  var INSPECT_KEY = "caltInspectMode";
  var EXCERPT_CAP = 1800;

  var sampleIndex = 0;
  var prevScore = 0;
  var warned = false;
  var stopped = false;
  var timer = null;
  var mode = "free";
  var inspectMode = false;
  var lastNative = null;

  function ext() {
    return typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;
  }

  function shouldSkipHost() {
    // Always score page text — SoftLand allow-list must not skip keyword locks.
    // (Force-porn DNR may still redirect first; if the page loads, keywords catch it.)
    if (typeof isCaltSpaUrl === "function" && isCaltSpaUrl(location.href)) return true;
    if (typeof isExtensionOrInternalUrl === "function" && isExtensionOrInternalUrl(location.href))
      return true;
    if (location.href.indexOf("locked.html") >= 0) return true;
    var host =
      typeof hostnameFromUrl === "function" ? hostnameFromUrl(location.href) : location.hostname;
    if (!host) return true;
    // Watch hosts: score only in Inspect (YouTube false positives otherwise).
    if (
      !inspectMode &&
      typeof isForceWatchHost === "function" &&
      isForceWatchHost(host) &&
      !(typeof isForcePornHost === "function" && isForcePornHost(host))
    ) {
      return true;
    }
    return false;
  }

  function collectHaystack() {
    var title = document.title || "";
    var body = "";
    try {
      body = (document.body && document.body.innerText) || "";
    } catch (e) {
      body = "";
    }
    body = String(body).slice(0, TEXT_CAP);
    var links = "";
    try {
      var as = document.querySelectorAll("a");
      var parts = [];
      var n = Math.min(as.length, 80);
      for (var i = 0; i < n; i++) {
        var t = (as[i].innerText || as[i].getAttribute("aria-label") || "").trim();
        if (t) parts.push(t.slice(0, 80));
      }
      links = parts.join(" ").slice(0, LINK_CAP);
    } catch (e2) {
      links = "";
    }
    return {
      title: title,
      body: body,
      links: links,
      hay: title + "\n" + body + "\n" + links,
    };
  }

  function showWarnBanner(score) {
    if (document.getElementById("calt-content-score-warn")) return;
    var el = document.createElement("div");
    el.id = "calt-content-score-warn";
    el.setAttribute("role", "status");
    el.textContent =
      "CALT: this page looks like a distraction (score " +
      score +
      "). Leave if you are studying.";
    el.style.cssText =
      "position:fixed;z-index:2147483646;left:12px;right:12px;bottom:12px;padding:12px 14px;" +
      "background:#1a1a1a;color:#f5f5f5;font:14px/1.4 system-ui,sans-serif;border-radius:8px;" +
      "box-shadow:0 4px 20px rgba(0,0,0,.35);";
    var btn = document.createElement("button");
    btn.textContent = "Dismiss";
    btn.style.cssText = "margin-left:12px;cursor:pointer;";
    btn.onclick = function () {
      el.remove();
    };
    el.appendChild(btn);
    (document.body || document.documentElement).appendChild(el);
  }

  function escapeHtml(s) {
    return String(s || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function highlightExcerpt(hay, terms) {
    var slice = String(hay || "").slice(0, EXCERPT_CAP);
    var escaped = escapeHtml(slice);
    var ordered = (terms || []).slice().sort(function (a, b) {
      return String(b).length - String(a).length;
    });
    for (var i = 0; i < ordered.length; i++) {
      var term = ordered[i];
      if (!term || term.length < 3) continue;
      var re = new RegExp(
        "(^|[^a-z0-9])(" + term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")([^a-z0-9]|$)",
        "gi"
      );
      escaped = escaped.replace(re, function (_m, a, mid, b) {
        return a + '<mark class="calt-hit">' + mid + "</mark>" + b;
      });
    }
    if (String(hay || "").length > EXCERPT_CAP) escaped += "…";
    return escaped;
  }

  function ensureInspectPanel() {
    var root = document.getElementById("calt-inspect-root");
    if (root) return root;
    root = document.createElement("div");
    root.id = "calt-inspect-root";
    root.innerHTML =
      '<style>' +
      "#calt-inspect-root{all:initial;position:fixed;z-index:2147483647;top:12px;right:12px;" +
      "width:min(420px,calc(100vw - 24px));max-height:calc(100vh - 24px);overflow:auto;" +
      "font:12px/1.4 ui-sans-serif,system-ui,Segoe UI,sans-serif;color:#e8e8f0;" +
      "background:rgba(12,12,18,.96);border:1px solid #3a3a4a;border-radius:10px;" +
      "box-shadow:0 12px 40px rgba(0,0,0,.45);}" +
      "#calt-inspect-root *{font:inherit;box-sizing:border-box;}" +
      "#calt-inspect-root .hd{display:flex;align-items:center;justify-content:space-between;" +
      "padding:10px 12px;border-bottom:1px solid #2a2a38;cursor:move;user-select:none;}" +
      "#calt-inspect-root .hd b{font-size:13px;letter-spacing:.02em;}" +
      "#calt-inspect-root .hd button{background:#222230;border:1px solid #444;color:#fff;" +
      "border-radius:6px;padding:3px 8px;cursor:pointer;}" +
      "#calt-inspect-root .bd{padding:10px 12px;}" +
      "#calt-inspect-root .row{margin:0 0 6px;}" +
      "#calt-inspect-root .muted{color:#9a9aaa;}" +
      "#calt-inspect-root .bar{height:8px;background:#222230;border-radius:99px;overflow:hidden;margin:4px 0 8px;}" +
      "#calt-inspect-root .bar>i{display:block;height:100%;background:linear-gradient(90deg,#f59e0b,#ef4444);}" +
      "#calt-inspect-root table{width:100%;border-collapse:collapse;margin:6px 0 10px;}" +
      "#calt-inspect-root th,#calt-inspect-root td{text-align:left;padding:3px 4px;border-bottom:1px solid #2a2a38;}" +
      "#calt-inspect-root th{color:#9a9aaa;font-weight:600;}" +
      "#calt-inspect-root .excerpt{white-space:pre-wrap;word-break:break-word;max-height:220px;" +
      "overflow:auto;background:#0a0a10;border:1px solid #2a2a38;border-radius:8px;padding:8px;}" +
      "#calt-inspect-root mark.calt-hit{background:#f59e0b;color:#111;padding:0 2px;border-radius:2px;}" +
      "#calt-inspect-root .pill{display:inline-block;padding:1px 7px;border-radius:99px;" +
      "background:#222230;border:1px solid #444;margin-right:4px;}" +
      "#calt-inspect-root .pill.lock{background:#7f1d1d;border-color:#b91c1c;}" +
      "#calt-inspect-root .pill.warn{background:#78350f;border-color:#d97706;}" +
      "#calt-inspect-root .pill.ok{background:#14532d;border-color:#16a34a;}" +
      "</style>" +
      '<div class="hd"><b>CALT Inspect</b><span><button type="button" id="calt-inspect-copy">Copy</button> ' +
      '<button type="button" id="calt-inspect-close">Close</button></span></div>' +
      '<div class="bd" id="calt-inspect-body">Sampling…</div>';
    (document.body || document.documentElement).appendChild(root);

    var dragging = false;
    var ox = 0;
    var oy = 0;
    var hd = root.querySelector(".hd");
    hd.addEventListener("mousedown", function (ev) {
      if (ev.target && ev.target.tagName === "BUTTON") return;
      dragging = true;
      var r = root.getBoundingClientRect();
      ox = ev.clientX - r.left;
      oy = ev.clientY - r.top;
      root.style.left = r.left + "px";
      root.style.top = r.top + "px";
      root.style.right = "auto";
    });
    window.addEventListener("mousemove", function (ev) {
      if (!dragging) return;
      root.style.left = Math.max(0, ev.clientX - ox) + "px";
      root.style.top = Math.max(0, ev.clientY - oy) + "px";
    });
    window.addEventListener("mouseup", function () {
      dragging = false;
    });

    root.querySelector("#calt-inspect-close").onclick = function () {
      ext().storage.local.set({ caltInspectMode: false });
      inspectMode = false;
      root.remove();
    };
    root.querySelector("#calt-inspect-copy").onclick = function () {
      var body = document.getElementById("calt-inspect-body");
      var text = body ? body.innerText : "";
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).catch(function () {});
      }
    };
    return root;
  }

  function paintInspect(parts, scored, action, thresholds) {
    ensureInspectPanel();
    var body = document.getElementById("calt-inspect-body");
    if (!body) return;
    var lock = thresholds.lock || 10;
    var warn = thresholds.warn || 5;
    var pctLock = lock > 0 ? Math.min(100, Math.round((scored.score / lock) * 1000) / 10) : 0;
    var pctWarn = warn > 0 ? Math.min(100, Math.round((scored.score / warn) * 1000) / 10) : 0;
    var pillClass = action === "lock" ? "lock" : action === "warn" ? "warn" : "ok";
    var host =
      typeof hostnameFromUrl === "function" ? hostnameFromUrl(location.href) : location.hostname;
    var forcePorn =
      typeof isForcePornHost === "function" ? isForcePornHost(host) : false;
    var cat =
      typeof classifyHostCategory === "function"
        ? classifyHostCategory(host, typeof browserPolicyOrFallback === "function"
            ? browserPolicyOrFallback(null)
            : {})
        : "?";

    var nativeLine = "native: …";
    if (lastNative) {
      nativeLine =
        "native: " +
        (lastNative.action || "?") +
        " · " +
        (lastNative.reason || "") +
        (lastNative.matched ? " · matched " + lastNative.matched : "") +
        (lastNative.softland_enabled === false ? " · SoftLand OFF" : "");
    }

    var rows = (scored.details || [])
      .slice(0, 24)
      .map(function (d) {
        return (
          "<tr><td>" +
          escapeHtml(d.term) +
          "</td><td>" +
          d.weight +
          "</td><td>" +
          d.hits +
          (d.used < d.hits ? "→" + d.used : "") +
          "</td><td>" +
          d.contrib +
          "</td><td>" +
          d.pct +
          "%</td></tr>"
        );
      })
      .join("");

    body.innerHTML =
      '<div class="row"><span class="pill ' +
      pillClass +
      '">' +
      escapeHtml(action) +
      "</span>" +
      '<span class="pill">mode ' +
      escapeHtml(mode) +
      "</span>" +
      '<span class="pill">sample #' +
      sampleIndex +
      "</span></div>" +
      '<div class="row"><b>Content score</b> ' +
      scored.score +
      " · warn@" +
      warn +
      " (" +
      pctWarn +
      "%) · lock@" +
      lock +
      " (" +
      pctLock +
      "%)</div>" +
      '<div class="bar"><i style="width:' +
      pctLock +
      '%"></i></div>' +
      '<div class="row muted">host <b style="color:#e8e8f0">' +
      escapeHtml(host) +
      "</b> · category " +
      escapeHtml(String(cat)) +
      (forcePorn ? " · FORCE_PORN" : "") +
      "</div>" +
      '<div class="row muted">' +
      escapeHtml(nativeLine) +
      "</div>" +
      '<div class="row muted">title/body/links chars: ' +
      (parts.title || "").length +
      "/" +
      (parts.body || "").length +
      "/" +
      (parts.links || "").length +
      " · hay " +
      (parts.hay || "").length +
      " (cap " +
      (TEXT_CAP + LINK_CAP) +
      ")</div>" +
      "<div class=\"row\"><b>Matched terms</b> <span class=\"muted\">(Inspect: adult lock still redirects)</span></div>" +
      (rows
        ? "<table><thead><tr><th>term</th><th>w</th><th>hits</th><th>pts</th><th>%</th></tr></thead><tbody>" +
          rows +
          "</tbody></table>"
        : '<div class="muted">No weighted terms hit yet.</div>') +
      '<div class="row"><b>Highlighted extract</b></div>' +
      '<div class="excerpt">' +
      highlightExcerpt(parts.hay, scored.matched) +
      "</div>";

    window.__caltLastInspect = {
      url: location.href,
      host: host,
      mode: mode,
      action: action,
      score: scored.score,
      thresholds: thresholds,
      pctLock: pctLock,
      matched: scored.matched,
      details: scored.details,
      native: lastNative,
      title: parts.title,
    };
  }

  function refreshNative() {
    try {
      ext().runtime.sendMessage({ type: "INSPECT_GET_MODE", url: location.href }, function (res) {
        if (res && res.native) lastNative = res.native;
      });
    } catch (e) {
      /* ignore */
    }
  }

  function tick() {
    if (stopped && !inspectMode) return;
    if (!inspectMode && shouldSkipHost()) return;
    if (inspectMode && shouldSkipHost()) return;
    if (typeof scorePageHaystack !== "function" || typeof decideContentScoreAction !== "function")
      return;

    var parts = collectHaystack();
    var scored = scorePageHaystack(parts.hay);
    var th =
      (typeof CONTENT_SCORE_THRESHOLDS !== "undefined" && CONTENT_SCORE_THRESHOLDS[mode]) ||
      { warn: 5, lock: 10, maxSamples: 3 };
    var action = decideContentScoreAction({
      score: scored.score,
      prevScore: prevScore,
      sampleIndex: sampleIndex,
      warned: warned,
      mode: mode,
    });

    function requestLock() {
      stopped = true;
      if (timer) clearInterval(timer);
      function resumeScoring(delayMs) {
        stopped = false;
        warned = true;
        sampleIndex = 0;
        prevScore = 0;
        if (!timer) timer = setInterval(tick, SAMPLE_MS);
        window.setTimeout(function () {
          if (!stopped) tick();
        }, typeof delayMs === "number" ? delayMs : 4000);
      }
      try {
        ext().runtime.sendMessage(
          {
            type: "CONTENT_SCORE_LOCK",
            score: scored.score,
            matched: scored.matched.slice(0, 8),
            url: location.href,
            title: (document.title || "").slice(0, 200),
            excerpt: String(parts.hay || "").slice(0, EXCERPT_CAP),
            force_hard: true,
          },
          function (res) {
            var err =
              typeof chrome !== "undefined" &&
              chrome.runtime &&
              chrome.runtime.lastError
                ? chrome.runtime.lastError
                : null;
            // Skipped (inspect/below) or redirect failed → keep scoring so allow-list pages still lock.
            if (err || !res || res.skipped || res.locked === false) {
              resumeScoring(res && res.skipped ? 12000 : 3000);
            }
          }
        );
      } catch (e2) {
        resumeScoring(3000);
      }
    }

    var hostNow =
      typeof hostnameFromUrl === "function" ? hostnameFromUrl(location.href) : location.hostname;
    var hardPornHost =
      (hostNow && typeof isForcePornHost === "function" && isForcePornHost(hostNow, null)) ||
      (hostNow && typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(hostNow, null));

    if (inspectMode) {
      paintInspect(parts, scored, action, th);
      if (sampleIndex % 2 === 0) refreshNative();
      // Inspect may score overlays — but hard porn / lock-threshold adult pages still redirect.
      if (action === "lock" && (hardPornHost || scored.score >= (th.lock || 10))) {
        requestLock();
        return;
      }
      prevScore = scored.score;
      sampleIndex += 1;
      return;
    }

    if (action === "warn") {
      warned = true;
      showWarnBanner(scored.score);
      try {
        ext().runtime.sendMessage({
          type: "CONTENT_SCORE_WARN",
          score: scored.score,
          matched: scored.matched.slice(0, 8),
          url: location.href.slice(0, 300),
        });
      } catch (e) {
        /* ignore */
      }
    }
    if (action === "lock") {
      var allowSoft =
        typeof allowHostContentLock === "function"
          ? allowHostContentLock({
              host: hostNow,
              url: location.href,
              title: parts.title,
              hay: parts.hay,
              matched: scored.matched,
              score: scored.score,
            })
          : { skip: false };
      if (allowSoft.skip && !hardPornHost) {
        stopped = true;
        if (timer) clearInterval(timer);
        prevScore = scored.score;
        return;
      }
      requestLock();
      return;
    }
    if (action === "stop") {
      stopped = true;
      if (timer) clearInterval(timer);
      prevScore = scored.score;
      sampleIndex += 1;
      return;
    }
    prevScore = scored.score;
    sampleIndex += 1;
  }

  function rearmIfNeeded() {
    if (inspectMode) {
      stopped = false;
      return;
    }
    if (!stopped) return;
    stopped = false;
    sampleIndex = 0;
    prevScore = 0;
    timer = setInterval(tick, SAMPLE_MS);
    tick();
  }

  function applyInspectFlag(on) {
    inspectMode = Boolean(on);
    if (inspectMode) {
      stopped = false;
      if (!timer) timer = setInterval(tick, SAMPLE_MS);
      ensureInspectPanel();
      refreshNative();
      tick();
    } else {
      var root = document.getElementById("calt-inspect-root");
      if (root) root.remove();
    }
  }

  function start() {
    ext().storage.local.get(["gateCache", INSPECT_KEY], function (res) {
      var gc = res.gateCache || {};
      var browser = gc.browser || {};
      var m = String(browser.mode || "").toLowerCase();
      mode = m === "study" || m === "bible" || m === "planning" ? "study" : "free";
      applyInspectFlag(Boolean(res[INSPECT_KEY]));
      if (!inspectMode && shouldSkipHost()) return;
      timer = setInterval(tick, SAMPLE_MS);
      tick();
    });

    try {
      ext().storage.onChanged.addListener(function (changes, area) {
        if (area !== "local" || !changes[INSPECT_KEY]) return;
        applyInspectFlag(Boolean(changes[INSPECT_KEY].newValue));
      });
    } catch (e0) {
      /* ignore */
    }

    var churnChars = 0;
    var churnNodes = 0;
    var churnReset = null;
    try {
      var mo = new MutationObserver(function (muts) {
        for (var i = 0; i < muts.length; i++) {
          churnNodes += (muts[i].addedNodes && muts[i].addedNodes.length) || 0;
          if (muts[i].type === "characterData") {
            churnChars += String((muts[i].target && muts[i].target.data) || "").length;
          }
        }
        if (!churnReset) {
          churnReset = setTimeout(function () {
            if (churnChars >= CHURN_CHARS || churnNodes >= CHURN_NODES) rearmIfNeeded();
            churnChars = 0;
            churnNodes = 0;
            churnReset = null;
          }, 1000);
        }
      });
      mo.observe(document.documentElement, {
        childList: true,
        subtree: true,
        characterData: true,
      });
    } catch (e3) {
      /* ignore */
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start);
  } else {
    start();
  }
})();
