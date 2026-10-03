import { enforcerCmd, bridgeAvailable } from "../bridge.js";
import { localYmd } from "../mirrors.js";
import { navigate } from "../router.js";
import {
  canConfirmPlan,
  confirmErrorMessage,
  nextHalfHourIso,
  planConfirmedToday,
  plannedMinutes,
} from "../day-loop.js";

function dayRange() {
  const d = localYmd();
  return { from: `${d}T00:00:00`, to: `${d}T23:59:59` };
}

function isFreeCategory(cat) {
  const c = String(cat || "").toLowerCase();
  return [
    "break",
    "free",
    "reward",
    "leisure",
    "rest",
    "downtime",
    "food",
    "sleep",
    "commute",
  ].includes(c);
}

/**
 * @param {HTMLElement} app
 * @param {object} status
 * @param {() => void} onRefresh
 */
export async function renderPlan(app, status, onRefresh) {
  const confirmed = planConfirmedToday(status.softland, status.loop);
  const gate = canConfirmPlan(status);
  let blocks = [];
  if (bridgeAvailable()) {
    const range = dayRange();
    const r = await enforcerCmd("plan.list", range, 5000);
    if (r && r.ok !== false && Array.isArray(r.blocks)) blocks = r.blocks;
  }

  const rows = blocks
    .map((b) => {
      const free = isFreeCategory(b.category);
      const start = (b.start_at || "").slice(11, 16);
      const end = (b.end_at || "").slice(11, 16);
      return `<li class="${free ? "muted" : ""}">
        <strong>${start}–${end}</strong> ${escapeHtml(b.title || "Untitled")}
        <span class="muted"> · ${escapeHtml(b.category || "study")} · ${b.planned_minutes || 0}m${free ? " (free — doesn’t count)" : ""}</span>
      </li>`;
    })
    .join("");

  app.innerHTML = `
    <div class="card" style="max-width:42rem">
      <h2 style="margin-top:0">Plan</h2>
      <p class="muted">Confirm needs ≥1 non-free planned minute today. Free/food/sleep blocks don’t count.</p>
      <p>Non-free planned: <strong>${plannedMinutes(status)}</strong> min · Confirmed: <strong>${confirmed ? "yes" : "no"}</strong></p>
      <div class="row" style="margin-top:0.75rem">
        <button type="button" class="btn" id="btn-add-60">+ 60m study</button>
        <button type="button" class="btn" id="btn-add-30">+ 30m personal</button>
        <button type="button" class="btn primary" id="btn-confirm" ${gate.ok && !confirmed ? "" : "disabled"}>
          ${confirmed ? "Already confirmed" : "Confirm plan"}
        </button>
      </div>
      <p class="error" id="plan-err" hidden></p>
      <ul class="block-list" style="margin-top:1rem;padding-left:1.1rem">
        ${rows || "<li class='muted'>No blocks today — add one above.</li>"}
      </ul>
      <p class="muted" style="margin-top:1rem"><a href="#/calendar">Calendar</a> (grid later) · <a href="#/home">Home</a></p>
    </div>
  `;

  const err = () => document.getElementById("plan-err");

  async function quickAdd(title, category, duration_minutes) {
    const e = err();
    e.hidden = true;
    const r = await enforcerCmd(
      "plan.upsert",
      { title, category, start_at: nextHalfHourIso(), duration_minutes },
      6000,
    );
    if (!r || r.ok === false || r.error) {
      e.textContent = r?.error || "Add failed";
      e.hidden = false;
      return;
    }
    onRefresh();
  }

  document.getElementById("btn-add-60").onclick = () =>
    quickAdd("Deep work", "study", 60);
  document.getElementById("btn-add-30").onclick = () =>
    quickAdd("Personal focus", "personal", 30);

  const conf = document.getElementById("btn-confirm");
  if (conf && !confirmed) {
    conf.onclick = async () => {
      conf.disabled = true;
      const e = err();
      e.hidden = true;
      const r = await enforcerCmd("day.confirm_plan", {}, 8000);
      if (!r || r.ok === false || r.error) {
        e.textContent = confirmErrorMessage(r?.error);
        e.hidden = false;
        conf.disabled = false;
        return;
      }
      navigate("home");
      onRefresh();
    };
  }
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}
