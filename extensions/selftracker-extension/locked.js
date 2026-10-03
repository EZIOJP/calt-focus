const FALLBACK_LINKS = [
  { title: "Bible reader", url: "http://localhost:5173/bible", source: "calt" },
  { title: "Today's plan", url: "http://localhost:5173/productivity?tab=plan", source: "calt" },
  { title: "Lecture notes", url: "http://localhost:5173/lecture-notes", source: "calt" },
  { title: "Review hub", url: "http://localhost:5173/review", source: "calt" },
  { title: "Scaler", url: "https://www.scaler.com/", source: "allowlist" },
  { title: "Colab", url: "https://colab.research.google.com/", source: "allowlist" },
  { title: "GitHub", url: "https://github.com/", source: "allowlist" },
];

function extRuntime() {
  return typeof chrome !== "undefined" && chrome.runtime
    ? chrome
    : typeof browser !== "undefined"
      ? browser
      : null;
}

function fmtMin(n) {
  if (n == null || Number.isNaN(Number(n))) return "—";
  const m = Math.max(0, Math.round(Number(n)));
  const h = Math.floor(m / 60);
  const r = m % 60;
  const hours = h === 1 ? "1 hour" : h + " hours";
  const mins = r === 1 ? "1 min" : r + " mins";
  if (h === 0) return hours + " " + mins;
  if (r === 0) return hours;
  return hours + " " + mins;
}

function renderStats(g) {
  const root = document.getElementById("stats");
  if (!g || g._offline) {
    root.innerHTML =
      '<div class="stat wide"><div class="k">Status</div>' +
      '<div class="v muted">Gate API unreachable — start CALT (:8000). Shortcuts below still work.</div></div>';
    return;
  }
  const prod = g.productive_minutes;
  const goal = g.daily_goal_minutes;
  const left = g.remaining_minutes;
  const modeRaw = ((g.browser && (g.browser.mode_label || g.browser.mode)) || g.browser_mode || "—");
  const mode = String(modeRaw).toUpperCase();
  const modeLc = String(modeRaw).toLowerCase();
  const pct = goal > 0 && prod != null ? Math.min(100, Math.round((Number(prod) / Number(goal)) * 100)) : 0;
  const bible = g.chapter_goal_met ? "Done" : "Needed";
  const block = g.current_block;
  const inFocusMode = modeLc === "study" || modeLc === "bible" || modeLc === "planning";
  const dayMet = Boolean(g.day_unlimited || g.reward_day || g.unlocked);

  // "Games unlocked" under "Time left to unlock" was confusing when STUDY still blocks YouTube.
  let unlockKey = "Time left to unlock";
  let unlockVal = "Locked";
  if (dayMet && inFocusMode) {
    unlockKey = "Daily goal";
    unlockVal = block && block.title
      ? "Met — still locked for this study block"
      : "Met — still locked in " + mode + " mode";
  } else if (dayMet) {
    unlockKey = "Daily unlock";
    unlockVal = g.reward_day
      ? "Reward day · YouTube & games OK"
      : "Goal met · YouTube & games OK";
  } else if (left != null) {
    unlockVal = fmtMin(left) + " study left";
  }

  let blockHtml = "";
  if (block && block.title) {
    const leftB = block.minutes_left != null ? " · " + fmtMin(block.minutes_left) + " left in block" : "";
    blockHtml =
      '<div class="stat wide"><div class="k">Now on plan</div>' +
      '<div class="v muted">' + escapeHtml(block.title) +
      (block.category ? " · " + escapeHtml(block.category) : "") +
      escapeHtml(leftB) + "</div></div>";
  }
  root.innerHTML =
    '<div class="stat"><div class="k">Studied today</div><div class="v">' + escapeHtml(fmtMin(prod)) + "</div></div>" +
    '<div class="stat"><div class="k">Daily goal</div><div class="v">' + escapeHtml(fmtMin(goal)) + "</div></div>" +
    '<div class="stat wide"><div class="k">' + escapeHtml(unlockKey) + "</div>" +
    '<div class="v accent">' + escapeHtml(unlockVal) + "</div>" +
    '<div class="bar"><span style="width:' + pct + '%"></span></div></div>' +
    '<div class="stat"><div class="k">Mode</div><div class="v">' + escapeHtml(mode) + "</div></div>" +
    '<div class="stat"><div class="k">Bible chapter</div><div class="v">' + escapeHtml(bible) + "</div></div>" +
    blockHtml;
}

function escapeHtml(s) {
  return String(s == null ? "" : s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function linkClass(source, index) {
  if (index < 2 && source === "calt") return "link primary";
  if (source === "goal") return "link goal";
  if (source === "notes" || source === "notes_link") return "link notes";
  return "link";
}

function renderLinks(links) {
  const root = document.getElementById("links");
  const list = Array.isArray(links) && links.length ? links : FALLBACK_LINKS;
  root.innerHTML = "";
  list.forEach(function (item, i) {
    if (!item || !item.url) return;
    const a = document.createElement("a");
    a.className = linkClass(item.source || "", i);
    a.href = item.url;
    a.textContent = item.title || item.url;
    // Same-tab navigate — avoid FOCUS_CALT / ensureOneCaltTab from this page.
    a.target = "_self";
    a.rel = "noopener";
    root.appendChild(a);
  });
}

function applyGate(g, fromCache) {
  const status = document.getElementById("status");
  const title = document.getElementById("title");
  const body = document.getElementById("body");
  if (!g || g._offline) {
    status.textContent = "Gate unreachable — start CALT API (:8000). Showing fallback shortcuts.";
    renderStats(g || { _offline: true });
    renderLinks(null);
    return;
  }
  const morning = g.morning || {};
  const browser = g.browser || {};
  const next = morning.next || browser.morning_next || "open";
  if (next === "bible") {
    title.textContent = "Morning — Bible first";
    body.textContent =
      "Finish today’s Bible chapter, then confirm your plan. Only this tab is locked; other tabs are untouched.";
  } else if (next === "plan") {
    title.textContent = "Morning — Confirm plan";
    body.textContent = "Review goals and confirm today’s plan. Pick a shortcut below — opens in this tab.";
  } else {
    title.textContent = "Distraction site blocked";
    const modeLc = String(browser.mode_label || browser.mode || "").toLowerCase();
    const dayMet = Boolean(g.day_unlimited || g.reward_day || g.unlocked);
    if (modeLc === "free" || (dayMet && modeLc !== "bible" && modeLc !== "planning")) {
      body.textContent =
        "Odd redirect in free mode — reload SelfTracker on Edge. Distractions stay blocked; YouTube should work after today's focus goal.";
    } else if (dayMet && (modeLc === "study" || modeLc === "bible" || modeLc === "planning")) {
      body.textContent =
        "Daily focus goal is met on the server, but this tab still sees study rules — reload SelfTracker on Edge so mode updates to FREE. Distractions stay blocked.";
    } else {
      body.textContent =
        "This tab was redirected. Other Edge tabs stay open. YouTube stays blocked until today's focus goal; distractions are always blocked.";
    }
  }
  const mode = (browser.mode_label || browser.mode || "").toUpperCase();
  const bits = ["Mode: " + (mode || "—")];
  if (g.locked) bits.push("games locked");
  bits.push("tab-local lock");
  if (fromCache) bits.push("cached");
  if (g.stale || g.degraded) bits.push("stale");
  status.textContent = bits.join(" · ");
  renderStats(g);
  renderLinks(g.suggested_links);
}

function parseBlockedHost() {
  try {
    const params = new URLSearchParams(window.location.search || "");
    let host = (params.get("host") || "").toLowerCase().replace(/^www\./, "");
    const from = params.get("from") || "";
    if (!host && from) {
      try {
        host = new URL(from).hostname.toLowerCase().replace(/^www\./, "");
      } catch (e) {
        host = "";
      }
    }
    return { host: host, from: from };
  } catch (e) {
    return { host: "", from: "" };
  }
}

function reopenUrl(host, from) {
  if (from) {
    try {
      const u = new URL(from);
      const fh = u.hostname.toLowerCase().replace(/^www\./, "");
      if (fh === host) return from;
    } catch (e) {
      /* fall through */
    }
  }
  return "https://" + host + "/";
}

function formatUntil(untilMs) {
  const d = new Date(untilMs);
  const hh = String(d.getHours()).padStart(2, "0");
  const mm = String(d.getMinutes()).padStart(2, "0");
  const ss = String(d.getSeconds()).padStart(2, "0");
  return hh + ":" + mm + ":" + ss;
}

function setTempMsg(text, kind) {
  const el = document.getElementById("tempAllowMsg");
  if (!el) return;
  el.textContent = text || "";
  el.className = "temp-msg" + (kind ? " " + kind : "");
}

let tempCountdownTimer = null;

function showAllowedUntil(untilMs) {
  const btn = document.getElementById("tempAllowBtn");
  const tick = function () {
    const left = Math.max(0, Math.ceil((untilMs - Date.now()) / 1000));
    if (left <= 0) {
      if (tempCountdownTimer) clearInterval(tempCountdownTimer);
      tempCountdownTimer = null;
      if (btn) {
        btn.disabled = false;
        btn.textContent = "Allow this site 60 sec";
      }
      setTempMsg("Temp allow expired — site will block again.", "");
      return;
    }
    if (btn) {
      btn.disabled = true;
      btn.textContent = "Allowed (" + left + "s left)";
    }
    setTempMsg("Allowed until " + formatUntil(untilMs), "ok");
  };
  tick();
  if (tempCountdownTimer) clearInterval(tempCountdownTimer);
  tempCountdownTimer = setInterval(tick, 500);
}

async function sendExtMessage(msg) {
  const api = extRuntime();
  if (!api || !api.runtime || !api.runtime.sendMessage) {
    return Promise.reject(new Error("no extension runtime"));
  }
  return new Promise(function (resolve, reject) {
    try {
      api.runtime.sendMessage(msg, function (resp) {
        const err = api.runtime.lastError;
        if (err) reject(new Error(err.message || "sendMessage failed"));
        else resolve(resp);
      });
    } catch (e) {
      reject(e);
    }
  });
}

async function initTempAllowUi() {
  const box = document.getElementById("tempAllowBox");
  const btn = document.getElementById("tempAllowBtn");
  const label = document.getElementById("tempHostLabel");
  if (!box || !btn || !label) return;

  const parsed = parseBlockedHost();
  const host = parsed.host;
  if (!host) {
    box.hidden = true;
    return;
  }

  box.hidden = false;
  label.textContent = host;

  // Local exclusion check (gate_policy.js loaded before this script).
  let excluded = false;
  try {
    if (typeof isTempAllowExcludedHost === "function") {
      const pol =
        typeof browserPolicyOrFallback === "function" ? browserPolicyOrFallback(null) : null;
      excluded = isTempAllowExcludedHost(host, pol);
    }
  } catch (e) {
    excluded = false;
  }

  if (excluded) {
    btn.disabled = true;
    btn.classList.add("hidden");
    setTempMsg("Watch / distraction / social sites can't be temporarily allowed.", "err");
    return;
  }

  btn.classList.remove("hidden");
  btn.disabled = false;
  btn.textContent = "Allow this site 60 sec";

  try {
    const status = await sendExtMessage({ type: "TEMP_ALLOW_STATUS", host: host });
    if (status && status.excluded) {
      btn.disabled = true;
      btn.classList.add("hidden");
      setTempMsg(
        status.error || "Watch / distraction / social sites can't be temporarily allowed.",
        "err",
      );
      return;
    }
    if (status && status.allowed_until && status.allowed_until > Date.now()) {
      showAllowedUntil(status.allowed_until);
    }
  } catch (e) {
    /* status optional */
  }

  btn.onclick = async function () {
    btn.disabled = true;
    setTempMsg("Allowing…", "");
    try {
      const resp = await sendExtMessage({ type: "TEMP_ALLOW_REQUEST", host: host });
      if (!resp || !resp.ok) {
        setTempMsg((resp && resp.error) || "Could not allow this site.", "err");
        btn.disabled = false;
        return;
      }
      showAllowedUntil(resp.until);
      // Re-open the blocked page in this tab while the 60s window is active.
      window.location.href = reopenUrl(host, parsed.from);
    } catch (err) {
      setTempMsg("Could not allow this site.", "err");
      btn.disabled = false;
    }
  };
}

async function refresh() {
  const api = extRuntime();
  // Prefer live gate; fall back to background gateCache (includes stats + links).
  try {
    const r = await fetch(GATE_API_URL, {
      cache: "no-store",
      headers: { "X-CALT-Extension": "selftracker-locked" },
    });
    if (!r.ok) throw new Error("HTTP " + r.status);
    const g = await r.json();
    applyGate(g, false);
    return;
  } catch (err) {
    /* try cache */
  }
  if (api && api.storage && api.storage.local) {
    try {
      const result = await new Promise(function (resolve) {
        api.storage.local.get(["gateCache"], resolve);
      });
      if (result && result.gateCache) {
        applyGate(result.gateCache, true);
        return;
      }
    } catch (e) { /* ignore */ }
  }
  applyGate({ _offline: true }, false);
}

refresh();
setInterval(refresh, 5000);
initTempAllowUi();
