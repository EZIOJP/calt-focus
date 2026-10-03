import { enforcerCmd } from "../bridge.js";
import { bibleDoneToday, localYmd } from "../mirrors.js";
import { navigate } from "../router.js";
import {
  canConfirmPlan,
  confirmErrorMessage,
  planConfirmedToday,
  plannedMinutes,
} from "../day-loop.js";

/**
 * @param {HTMLElement} app
 * @param {object} status
 * @param {() => void} onRefresh
 */
export function renderHome(app, status, onRefresh) {
  const minsTracked =
    status.rollup?.productive_minutes ?? status.loop?.tracked_productive_minutes ?? 0;
  const focus = status.softland?.goals?.daily_focus_minutes ?? 180;
  const bible = bibleDoneToday(status.softland, status.loop);
  const plan = planConfirmedToday(status.softland, status.loop);
  const planned = plannedMinutes(status);
  const softOn = Boolean(status.softland?.softland_enabled);
  const gate = canConfirmPlan(status);
  const showConfirm = !plan;

  app.innerHTML = `
    <div class="card">
      <h2 style="margin-top:0">Home</h2>
      <p class="muted">Today ${localYmd()} · wakeup → bible → plan → Confirm</p>
      <p>Tracked productive: <strong>${minsTracked}</strong> / ${focus} min</p>
      <p>Bible today: <strong>${bible ? "done" : "pending"}</strong></p>
      <p>Planned non-free: <strong>${planned}</strong> min ${planned > 0 ? "" : "(need ≥1 to Confirm)"}</p>
      <p>Plan confirmed: <strong>${plan ? "yes" : "no"}</strong></p>
      <p>SoftLand (sites): <strong>${softOn ? "on" : "off"}</strong> · Arm couples only on Confirm</p>
      ${
        showConfirm
          ? `<div class="row" style="margin-top:1rem">
        <button type="button" class="btn" id="btn-go-bible" ${bible ? "hidden" : ""}>Bible</button>
        <button type="button" class="btn" id="btn-go-plan">Plan</button>
        <button type="button" class="btn primary" id="btn-confirm" ${gate.ok ? "" : "disabled"}>Confirm plan</button>
      </div>
      <p class="muted" id="confirm-hint">${
        !bible
          ? "Mark morning Bible done first."
          : planned <= 0
            ? "Add a non-free plan block (Plan tab or +60m on the morning overlay)."
            : "Ready — Confirm turns SoftLand + Arm on."
      }</p>`
          : `<p class="muted" style="margin-top:1rem">Day ritual complete. Sites follow SoftLand; apps follow Arm kill list.</p>`
      }
      <p class="error" id="home-err" hidden></p>
    </div>
  `;

  const goBible = document.getElementById("btn-go-bible");
  if (goBible) goBible.onclick = () => navigate("bible");
  const goPlan = document.getElementById("btn-go-plan");
  if (goPlan) goPlan.onclick = () => navigate("plan");

  const conf = document.getElementById("btn-confirm");
  if (conf) {
    conf.onclick = async () => {
      conf.disabled = true;
      const err = document.getElementById("home-err");
      err.hidden = true;
      const r = await enforcerCmd("day.confirm_plan", {}, 8000);
      if (!r || r.ok === false || r.error) {
        err.textContent = confirmErrorMessage(r?.error);
        err.hidden = false;
        conf.disabled = false;
        return;
      }
      onRefresh();
    };
  }
}
