/* Popup UI — must be an external file (MV3 CSP: script-src 'self'). */
const api = typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;
const CATEGORY_COLORS = {
  "Dev / Docs": "#00ff88",
  Research: "#44ffcc",
  Coursework: "#88ffaa",
  "Knowledge Work": "#aaffcc",
  Design: "#44aaff",
  "Admin / Email": "#aaaaff",
  Communication: "#8888ff",
  News: "#ffcc44",
  Browsing: "#ff8844",
  Music: "#ff44ff",
  Shopping: "#ff6644",
  "Social Media": "#ff4444",
  "Video / Streaming": "#ff6644",
  Gaming: "#ff8844",
  "Idle / New Tab": "#333350",
  Unknown: "#444460",
};

function formatMin(seconds) {
  return Math.round(seconds / 60);
}

function renderGate(gateCache, redirectsEnabled) {
  const chip = document.getElementById("gateChip");
  const meta = document.getElementById("gateMeta");
  const toggle = document.getElementById("redirectsToggle");
  const banner = document.getElementById("enforceBanner");
  toggle.checked = redirectsEnabled !== false;
  const setBanner = (on, text) => {
    if (!banner) return;
    banner.className = on ? "enforce-banner on" : "enforce-banner";
    if (text) banner.textContent = text;
  };
  if (!gateCache || (gateCache.ok === false && !gateCache.degraded)) {
    chip.className = "gate-chip down";
    chip.textContent = "API down";
    meta.textContent = "Start CALT backend (:8000). Cold Turkey is backup for sites.";
    setBanner(false);
    return;
  }
  const browserGate = gateCache.browser || {};
  const modeRaw = String(browserGate.mode || "").toLowerCase();
  const mode = (browserGate.mode_label || modeRaw.toUpperCase() || "").trim();
  const incubating = Boolean(
    (gateCache.incubation && gateCache.incubation.active) || browserGate.incubation_active
  );
  const enforce = Boolean(
    gateCache.enforce || browserGate.enforce || gateCache.degraded || incubating
  );
  const ytBlocked =
    incubating ||
    Boolean(browserGate.block_watch_sites) ||
    ["bible", "planning", "study"].includes(modeRaw);
  if (incubating && redirectsEnabled !== false) {
    setBanner(true, "INCUBATION · ENTERTAINMENT BLOCKED");
  } else if (ytBlocked && redirectsEnabled !== false) {
    setBanner(true, "THIS TAB · YT BLOCKED");
  } else if (modeRaw === "free") {
    setBanner(false);
  } else {
    setBanner(ytBlocked, "THIS TAB · YT BLOCKED");
  }
  if (gateCache.stale || gateCache.degraded) {
    chip.className = "gate-chip enforce";
    chip.textContent = (mode || "STUDY") + " · fail-closed";
    meta.textContent =
      "Gate API unreachable — keeping last mode; blocked sites rewrite this tab only. " +
      (gateCache.error || "");
    return;
  }
  if (!gateCache.enabled && !enforce) {
    chip.className = "gate-chip open";
    chip.textContent = mode ? mode + " · gate soft" : "Hard-block off";
    meta.textContent =
      "App kills: CALT Desktop Enforcer. Site SoftLand: CALT Gate. Arm hard-block in Desktop Rules.";
    return;
  }
  if (mode) {
    chip.className = ytBlocked
      ? "gate-chip enforce"
      : gateCache.locked || browserGate.block_other
        ? "gate-chip locked"
        : "gate-chip open";
    chip.textContent = incubating
      ? "INCUBATION"
      : ytBlocked
        ? "THIS TAB · " + mode + " · YT"
        : mode + (enforce ? " · tab lock" : "");
    const bits = [];
    if (incubating) {
      const rem = (gateCache.incubation && gateCache.incubation.remaining_sec) || 0;
      bits.push(rem > 0 ? "break " + rem + "s left" : "incubation");
      bits.push("open CALT Desktop · Focus");
    } else {
      if (ytBlocked) bits.push("blocked sites → this tab only");
      if (browserGate.block_other) bits.push("allowlist only");
      else if (browserGate.block_porn) bits.push("distraction filter");
      if (gateCache.locked) bits.push("games locked");
      const morning = (gateCache.morning && gateCache.morning.next) || browserGate.morning_next;
      if (morning && morning !== "open") bits.push("next: " + morning);
      if (gateCache.desktop && gateCache.desktop.enforcer_owns_kills) bits.push("enforcer on");
    }
    meta.textContent = bits.join(" · ") || "Mode from distraction-gate";
    return;
  }
  if (gateCache.locked) {
    chip.className = "gate-chip locked";
    chip.textContent = "Games locked · other tabs free";
    const left =
      gateCache.remaining_minutes != null ? gateCache.remaining_minutes + "m study left" : "";
    const bible = gateCache.chapter_goal_met ? "Bible done" : "Bible chapter needed";
    meta.textContent = [left, bible].filter(Boolean).join(" · ");
  } else {
    chip.className = "gate-chip open";
    chip.textContent = "Unlocked";
    meta.textContent = "Blocked sites rewrite only the offending tab.";
  }
}

function load() {
  api.storage.local.get(
    ["dailyLog", "tabSwitchCount", "liveEntry", "gateCache", "redirectsEnabled", "lastJarvisLine"],
    (result) => {
      const log = result.dailyLog || [];
      const live = result.liveEntry;
      renderGate(result.gateCache, result.redirectsEnabled);
      const jl = document.getElementById("jarvisLine");
      if (jl) {
        const t = (result.lastJarvisLine || "").trim();
        jl.textContent = t ? "Jarvis: " + t : "";
      }

      if (live) {
        try {
          const domain = new URL(live.url).hostname.replace("www.", "");
          document.getElementById("currentSite").textContent = domain || live.title;
        } catch {
          document.getElementById("currentSite").textContent = live.title || "Unknown";
        }
        document.getElementById("currentCategory").textContent = live.category || "—";
      }

      document.getElementById("totalSessions").textContent = log.length;
      document.getElementById("tabSwitches").textContent = result.tabSwitchCount || 0;

      const PRODUCTIVE = ["Dev / Docs", "Research", "Coursework", "Knowledge Work", "Design"];
      const LEISURE = ["Social Media", "Video / Streaming", "Gaming", "Shopping", "Music"];

      const prodSec = log
        .filter((e) => PRODUCTIVE.includes(e.category))
        .reduce((s, e) => s + e.duration_seconds, 0);
      const leiSec = log
        .filter((e) => LEISURE.includes(e.category))
        .reduce((s, e) => s + e.duration_seconds, 0);

      document.getElementById("productiveTime").textContent = formatMin(prodSec);
      document.getElementById("leisureTime").textContent = formatMin(leiSec);

      const catTotals = {};
      log.forEach((e) => {
        catTotals[e.category] = (catTotals[e.category] || 0) + e.duration_seconds;
      });

      const sorted = Object.entries(catTotals)
        .sort((a, b) => b[1] - a[1])
        .slice(0, 5);
      const maxVal = sorted[0]?.[1] || 1;

      if (sorted.length > 0) {
        const list = document.getElementById("catList");
        list.replaceChildren();
        sorted.forEach(([cat, sec]) => {
          const row = document.createElement("div");
          row.className = "cat-row";
          const name = document.createElement("span");
          name.className = "cat-name";
          name.textContent = cat;
          const wrap = document.createElement("div");
          wrap.className = "cat-bar-wrap";
          const bar = document.createElement("div");
          bar.className = "cat-bar";
          bar.style.width = `${Math.round((sec / maxVal) * 100)}%`;
          bar.style.background = CATEGORY_COLORS[cat] || "#4444aa";
          wrap.appendChild(bar);
          const time = document.createElement("span");
          time.className = "cat-time";
          time.textContent = `${formatMin(sec)}m`;
          row.appendChild(name);
          row.appendChild(wrap);
          row.appendChild(time);
          list.appendChild(row);
        });
      }
    },
  );
  api.runtime.sendMessage({ type: "REFRESH_GATE" }, () => {});
}

document.getElementById("redirectsToggle").addEventListener("change", (e) => {
  api.runtime.sendMessage({ type: "SET_REDIRECTS", enabled: e.target.checked });
});

document.getElementById("openDash").addEventListener("click", () => {
  api.tabs.create({ url: api.runtime.getURL("dashboard.html") });
});

document.getElementById("openBibleBtn")?.addEventListener("click", () => {
  api.runtime.sendMessage({ type: "FOCUS_CALT", path: "/bible" }, () => void api.runtime.lastError);
});
document.getElementById("openPlanBtn")?.addEventListener("click", () => {
  api.runtime.sendMessage(
    { type: "FOCUS_CALT", path: "/productivity?tab=plan" },
    () => void api.runtime.lastError,
  );
});

document.getElementById("exportBtn").addEventListener("click", () => {
  api.storage.local.get(["dailyLog"], (result) => {
    const log = result.dailyLog || [];
    if (!log.length) {
      alert("No data to export yet.");
      return;
    }

    const headers = Object.keys(log[0]).join(",");
    const rows = log.map((e) => Object.values(e).map((v) => `"${v}"`).join(","));
    const csv = [headers, ...rows].join("\n");

    const blob = new Blob([csv], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `DSC_browser_behavior_${new Date().toISOString().slice(0, 10)}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  });
});

load();
setInterval(load, 5000);
