(function () {
  "use strict";
  if (window.__caltContentScoreArmed) return;
  window.__caltContentScoreArmed = true;

  var SAMPLE_MS = 2500;
  var TEXT_CAP = 6000;
  var LINK_CAP = 2000;
  var CHURN_CHARS = 800;
  var CHURN_NODES = 40;

  var sampleIndex = 0;
  var prevScore = 0;
  var warned = false;
  var stopped = false;
  var timer = null;
  var mode = "free";

  function ext() {
    return typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;
  }

  function shouldSkipHost() {
    var host =
      typeof hostnameFromUrl === "function" ? hostnameFromUrl(location.href) : location.hostname;
    if (!host) return true;
    if (typeof isForcePornHost === "function" && isForcePornHost(host)) return true;
    if (typeof isForceWatchHost === "function" && isForceWatchHost(host)) return true;
    if (typeof isCaltSpaUrl === "function" && isCaltSpaUrl(location.href)) return true;
    if (typeof isExtensionOrInternalUrl === "function" && isExtensionOrInternalUrl(location.href))
      return true;
    if (location.href.indexOf("locked.html") >= 0) return true;
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
    return title + "\n" + body + "\n" + links;
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

  function tick() {
    if (stopped || shouldSkipHost()) return;
    if (typeof scorePageHaystack !== "function" || typeof decideContentScoreAction !== "function")
      return;

    var hay = collectHaystack();
    var scored = scorePageHaystack(hay);
    var action = decideContentScoreAction({
      score: scored.score,
      prevScore: prevScore,
      sampleIndex: sampleIndex,
      warned: warned,
      mode: mode,
    });

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
      stopped = true;
      if (timer) clearInterval(timer);
      try {
        ext().runtime.sendMessage({
          type: "CONTENT_SCORE_LOCK",
          score: scored.score,
          matched: scored.matched.slice(0, 8),
          url: location.href,
        });
      } catch (e2) {
        /* ignore */
      }
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
    if (!stopped) return;
    stopped = false;
    sampleIndex = 0;
    prevScore = 0;
    timer = setInterval(tick, SAMPLE_MS);
    tick();
  }

  function start() {
    if (shouldSkipHost()) return;
    ext().storage.local.get(["gateCache"], function (res) {
      var gc = res.gateCache || {};
      var browser = gc.browser || {};
      var m = String(browser.mode || "").toLowerCase();
      mode = m === "study" || m === "bible" || m === "planning" ? "study" : "free";
      timer = setInterval(tick, SAMPLE_MS);
      tick();
    });

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
