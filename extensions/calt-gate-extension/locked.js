const FALLBACK_LINKS = [
  { title: "Bible reader", url: "http://127.0.0.1:5180/bible", source: "calt" },
  { title: "Today's plan", url: "http://127.0.0.1:5180/productivity?tab=plan", source: "calt" },
  { title: "Focus Home", url: "http://127.0.0.1:5180/productivity", source: "calt" },
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

function hasDailyGoal(g) {
  if (!g || g._offline) return false;
  if (g.has_daily_goal === false) return false;
  if (g.planning_enabled === false) return false;
  if (g.browser && g.browser.planning_enabled === false) return false;
  const goal = Number(g.daily_goal_minutes);
  return Number.isFinite(goal) && goal > 0;
}

function renderStats(g) {
  const root = document.getElementById("stats");
  const section = document.getElementById("statsSection");
  if (!g || g._offline) {
    if (section) section.hidden = false;
    root.innerHTML =
      '<div class="stat wide"><div class="k">Status</div>' +
      '<div class="v muted">Gate unreachable — shortcuts below still work. SoftLand uses Focus + msg_host (no Study API).</div></div>';
    return;
  }
  const prod = g.productive_minutes;
  const goal = g.daily_goal_minutes;
  const left = g.remaining_minutes;
  const showGoal = hasDailyGoal(g);
  const modeRaw = ((g.browser && (g.browser.mode_label || g.browser.mode)) || g.browser_mode || "—");
  const mode = String(modeRaw).toUpperCase();
  const modeLc = String(modeRaw).toLowerCase();
  const pct =
    showGoal && goal > 0 && prod != null
      ? Math.min(100, Math.round((Number(prod) / Number(goal)) * 100))
      : 0;
  const bible = g.chapter_goal_met ? "Done" : "Needed";
  const block = g.current_block;
  const inFocusMode = modeLc === "study" || modeLc === "bible" || modeLc === "planning";
  const dayMet = Boolean(g.day_unlimited || g.reward_day || g.unlocked);
  const incubating = Boolean((g.incubation && g.incubation.active) || (g.browser && g.browser.incubation_active));
  const morningNext = String((g.morning && g.morning.next) || "").toLowerCase();

  let unlockKey = "Status";
  let unlockVal = "Locked";
  let showUnlock = true;
  if (incubating) {
    unlockKey = "Incubation break";
    const rem = (g.incubation && g.incubation.remaining_sec) || 0;
    unlockVal = rem > 0
      ? Math.round(rem / 60) + " min left · entertainment blocked"
      : "Entertainment blocked — open CALT Desktop · Focus";
  } else if (morningNext === "bible") {
    unlockKey = "Morning gate";
    unlockVal = "Finish Bible chapter to continue";
  } else if (morningNext === "plan") {
    unlockKey = "Morning gate";
    unlockVal = "Confirm today’s plan to continue";
  } else if (showGoal && dayMet && inFocusMode) {
    unlockKey = "Daily goal";
    unlockVal = block && block.title
      ? "Met — still locked for this study block"
      : "Met — still locked in " + mode + " mode";
  } else if (showGoal && dayMet) {
    unlockKey = "Daily unlock";
    unlockVal = g.reward_day
      ? "Reward day · YouTube & games OK"
      : "Goal met · YouTube & games OK";
  } else if (showGoal && left != null) {
    unlockKey = "Time left to unlock";
    unlockVal = fmtMin(left) + " study left";
  } else {
    showUnlock = morningNext === "bible" || morningNext === "plan" || incubating;
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

  let html = "";
  if (showGoal) {
    html +=
      '<div class="stat"><div class="k">Studied today</div><div class="v">' +
      escapeHtml(fmtMin(prod)) +
      "</div></div>" +
      '<div class="stat"><div class="k">Daily goal</div><div class="v">' +
      escapeHtml(fmtMin(goal)) +
      "</div></div>";
  } else {
    html +=
      '<div class="stat wide"><div class="k">Studied today</div><div class="v">' +
      escapeHtml(fmtMin(prod)) +
      "</div></div>";
  }
  if (showUnlock) {
    html +=
      '<div class="stat wide"><div class="k">' +
      escapeHtml(unlockKey) +
      "</div>" +
      '<div class="v accent">' +
      escapeHtml(unlockVal) +
      "</div>";
    if (showGoal) {
      html += '<div class="bar"><span style="width:' + pct + '%"></span></div>';
    }
    html += "</div>";
  }
  html +=
    '<div class="stat"><div class="k">Mode</div><div class="v">' +
    escapeHtml(mode) +
    "</div></div>" +
    '<div class="stat"><div class="k">Bible chapter</div><div class="v">' +
    escapeHtml(bible) +
    "</div></div>" +
    blockHtml;
  if (section) section.hidden = false;
  root.innerHTML = html;
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
  const parsed = parseBlockedHost();
  const hardPornHost =
    parsed.host &&
    typeof isAppealHardDenyHost === "function" &&
    isAppealHardDenyHost(parsed.host, (g && g.browser) || null);
  const pornWhy =
    parsed.why &&
    typeof isPornAppealReason === "function" &&
    isPornAppealReason(parsed.why);
  if (!g || g._offline) {
    status.textContent = "Gate unreachable — SoftLand uses Focus + msg_host. Showing fallback shortcuts.";
    renderStats(g || { _offline: true });
    renderLinks(null);
    return;
  }
  const morning = g.morning || {};
  const browser = g.browser || {};
  const next = morning.next || browser.morning_next || "open";
  const showGoal = hasDailyGoal(g);

  const whyLc = String(parsed.why || "").toLowerCase();
  const studyWhy =
    whyLc === "study" ||
    whyLc === "watch_study" ||
    whyLc === "watch" ||
    whyLc === "blocked" ||
    whyLc === "keyword";
  if (hardPornHost || (pornWhy && !studyWhy)) {
    title.textContent = "Adult content blocked";
    body.textContent =
      (parsed.host ? parsed.host + " — " : "") +
      "SoftLand blocked this as adult / porn. Quinn appeal is below if Focus Quinn is ON. Other tabs stay open.";
    if (next === "bible") {
      body.textContent += " Morning Bible is also still required today.";
    }
  } else if (studyWhy) {
    title.textContent = "Distraction site blocked";
    body.textContent =
      (parsed.host ? parsed.host + " — " : "") +
      "Blocked in study mode (SoftLand watch / focus rules), not as adult content. Other tabs stay open.";
  } else if (next === "bible") {
    title.textContent = "Morning — Bible first";
    body.textContent = showGoal
      ? "Finish today’s Bible chapter, then confirm your plan. Only this tab is locked; other tabs are untouched."
      : "Finish today’s Bible chapter. Planning goals are off — no daily focus unlock on this page. Only this tab is locked.";
  } else if (next === "plan") {
    title.textContent = "Morning — Confirm plan";
    body.textContent = "Review goals and confirm today’s plan. Pick a shortcut below — opens in this tab.";
  } else {
    title.textContent = "Distraction site blocked";
    const modeLc = String(browser.mode_label || browser.mode || "").toLowerCase();
    const dayMet = Boolean(g.day_unlimited || g.reward_day || g.unlocked);
    const incubating = Boolean((g.incubation && g.incubation.active) || browser.incubation_active);
    if (incubating) {
      title.textContent = "Incubation break";
      body.textContent =
        "Mandatory rest — entertainment stays blocked. Stretch or walk. Manage timers in CALT Desktop · Focus (Windows enforcer owns app kills).";
    } else if (modeLc === "free" || (dayMet && modeLc !== "bible" && modeLc !== "planning")) {
      body.textContent =
        "Odd redirect in free mode — reload CALT Gate on Edge. Distractions stay blocked.";
    } else if (showGoal && dayMet && (modeLc === "study" || modeLc === "bible" || modeLc === "planning")) {
      body.textContent =
        "Daily focus goal is met, but this tab still sees study rules — reload CALT Gate so mode updates to FREE.";
    } else {
      body.textContent = showGoal
        ? "This tab was redirected. Control blocks in CALT Desktop · Focus. Distractions stay blocked until SoftLand opens."
        : "This tab was redirected. Control blocks in CALT Desktop · Focus. No daily focus goal is set.";
    }
  }
  const mode = (browser.mode_label || browser.mode || "").toUpperCase();
  const bits = ["Mode: " + (mode || "—")];
  if ((g.incubation && g.incubation.active) || browser.incubation_active) bits.push("incubation");
  if (g.locked) bits.push("games locked");
  bits.push("tab-local lock");
  if (g.desktop && g.desktop.enforcer_owns_kills) bits.push("enforcer on");
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
    const why = (params.get("why") || "").trim();
    const until = (params.get("until") || "").trim();
    if (!host && from) {
      try {
        host = new URL(from).hostname.toLowerCase().replace(/^www\./, "");
      } catch (e) {
        host = "";
      }
    }
    return { host: host, from: from, why: why, until: until };
  } catch (e) {
    return { host: "", from: "", why: "", until: "" };
  }
}

function showRedirectDialog(parsed) {
  const dlg = document.getElementById("redirectDialog");
  const hostEl = document.getElementById("redirectHost");
  const detailEl = document.getElementById("redirectDetail");
  const btn = document.getElementById("redirectDismiss");
  if (!dlg || !hostEl || !detailEl || !btn) return;
  const host = parsed.host || "unknown site";
  const from = parsed.from || "";
  const why = parsed.why || "";
  const until = parsed.until || "";
  hostEl.textContent = host;
  const whyLc = String(why || "").toLowerCase();
  let whyHuman = why;
  if (whyLc === "study" || whyLc === "watch_study" || whyLc === "watch") {
    whyHuman = "study mode (not porn)";
  } else if (whyLc === "blocked" || whyLc === "keyword") {
    whyHuman = whyLc === "keyword" ? "keyword filter" : "SoftLand block";
  } else if (whyLc === "sensitive") {
    whyHuman = "sensitive site";
  }
  const whyLabel = whyHuman ? "Reason: " + whyHuman + ". " : "";
  const untilLabel = until ? " Until: " + until + "." : "";
  detailEl.textContent =
    whyLabel +
    "CALT SoftLand blocked this webpage. Your browser stayed open — open Focus to change SoftLand rules." +
    untilLabel +
    (from ? " Logged for your history." : "");
  dlg.classList.remove("hidden");
  btn.onclick = function () {
    dlg.classList.add("hidden");
  };
}

function reportLockedPageLoad() {
  const parsed = parseBlockedHost();
  showRedirectDialog(parsed);
  // Study :8000 logging removed — Gate is native SoftLand only.
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

function showAllowedUntil(untilMs, reopen) {
  const btn = document.getElementById("tempAllowBtn");
  const tick = function () {
    const left = Math.max(0, Math.ceil((untilMs - Date.now()) / 1000));
    if (left <= 0) {
      if (tempCountdownTimer) clearInterval(tempCountdownTimer);
      tempCountdownTimer = null;
      if (btn) {
        btn.disabled = false;
        btn.textContent = "Allow this site 60 sec";
        btn.onclick = null;
      }
      setTempMsg("Temp allow expired — site will block again.", "");
      return;
    }
    if (btn) {
      btn.disabled = false;
      btn.textContent = "Open site (" + left + "s left)";
      if (reopen) btn.onclick = reopen;
    }
    setTempMsg("Allowed until " + formatUntil(untilMs) + " — open the site to go back.", "ok");
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

  let alreadyAllowed = false;
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
      alreadyAllowed = true;
      const go = function () {
        window.location.href = reopenUrl(host, parsed.from);
      };
      showAllowedUntil(status.allowed_until, go);
      const explicit =
        typeof urlLooksExplicit === "function" && urlLooksExplicit(parsed.from || "", "");
      const reopenKey = "caltReopen:" + host;
      if (!explicit && sessionStorage.getItem(reopenKey) !== "1") {
        sessionStorage.setItem(reopenKey, "1");
        go();
      }
    }
  } catch (e) {
    /* status optional */
  }

  if (alreadyAllowed) return;

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
      const go = function () {
        window.location.href = reopenUrl(host, parsed.from);
      };
      showAllowedUntil(resp.until, go);
      // Re-open the blocked page in this tab while the 60s window is active.
      go();
    } catch (err) {
      setTempMsg("Could not allow this site.", "err");
      btn.disabled = false;
    }
  };
}

async function refresh() {
  // Native-first: ask SW gateCache (populated by get_softland_doc). No Study :8000.
  try {
    const resp = await sendExtMessage({ type: "GET_GATE" });
    if (resp && resp.gateCache) {
      applyGate(resp.gateCache, Boolean(resp.gateCache.stale || resp.gateCache.degraded));
      return;
    }
  } catch (err) {
    /* fall through */
  }
  const api = extRuntime();
  if (api && api.storage && api.storage.local) {
    try {
      const result = await new Promise(function (resolve) {
        api.storage.local.get(["gateCache"], resolve);
      });
      if (result && result.gateCache) {
        applyGate(result.gateCache, true);
        return;
      }
    } catch (e) {
      /* ignore */
    }
  }
  applyGate({ _offline: true }, false);
}

function setAppealMsg(text, kind) {
  const el = document.getElementById("appealMsg");
  if (!el) return;
  el.textContent = text || "";
  el.className = "temp-msg" + (kind ? " " + kind : "");
}

async function initAppealUi() {
  const box = document.getElementById("appealBox");
  const btn = document.getElementById("appealBtn");
  const meta = document.getElementById("appealMeta");
  const noteEl = document.getElementById("appealNote");
  if (!box || !btn) return;

  const parsed = parseBlockedHost();
  const host = parsed.host;
  const why = parsed.why || "";
  const canAppeal =
    host &&
    (typeof isPornAppealReason !== "function" || isPornAppealReason(why) || !why);

  // Show appeal for porn / content_score / hard-deny — user may not trust auto-block fully.
  let show = false;
  if (host && typeof isPornAppealReason === "function" && isPornAppealReason(why)) show = true;
  if (host && typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, null))
    show = true;
  if (host && typeof isForcePornHost === "function" && isForcePornHost(host, null)) show = true;
  if (!show && canAppeal && (why === "porn" || why.indexOf("porn") >= 0 || why === "content_score"))
    show = true;

  if (!show) {
    box.hidden = true;
    return;
  }

  box.hidden = false;
  btn.disabled = false;
  if (noteEl) noteEl.disabled = false;
  if (meta) {
    meta.textContent =
      "Blocked" +
      (why ? " (" + why + ")" : "") +
      " · " +
      host +
      ". Gate v2: trusted/search may open now; everything else waits out a cooldown. Your note is logged, not authority.";
  }
  if (btn) btn.textContent = "Request review";
  setAppealMsg("", "");

  btn.onclick = async function () {
    btn.disabled = true;
    setAppealMsg("Submitting review…", "");
    const note = noteEl ? String(noteEl.value || "").slice(0, 400) : "";
    try {
      const resp = await sendExtMessage({
        type: "PORN_APPEAL_REQUEST",
        host: host,
        url: parsed.from || "https://" + host + "/",
        reason: why || "porn",
        note: note,
        title: "",
        excerpt: "",
      });
      if (!resp) {
        setAppealMsg("Appeal failed.", "err");
        btn.disabled = false;
        return;
      }
      if (resp.allow) {
        setAppealMsg(
          (resp.why || "Opened for this path") +
            (resp.until ? " — until " + formatUntil(resp.until) : ""),
          "ok"
        );
        window.setTimeout(function () {
          window.location.href = reopenUrl(host, parsed.from);
        }, 600);
        return;
      }
      setAppealMsg(resp.why || "Review queued or denied.", resp.queued ? "" : "err");
      btn.disabled = false;
      if (resp.queued && resp.readyAt) {
        btn.textContent = "Check review status";
      }
    } catch (err) {
      setAppealMsg((err && err.message) || "Appeal failed.", "err");
      btn.disabled = false;
    }
  };
}

refresh();
setInterval(refresh, 8000);
// Gate v2 removed 60s temp-allow — hide the box.
(function hideTempAllowV2() {
  const box = document.getElementById("tempAllowBox");
  if (box) {
    box.hidden = true;
    box.style.display = "none";
  }
})();
initAppealUi();
reportLockedPageLoad();
