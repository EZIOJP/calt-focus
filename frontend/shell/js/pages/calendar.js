import { navigate } from "../router.js";
import { plannedMinutes, planConfirmedToday } from "../day-loop.js";

/**
 * @param {HTMLElement} app
 * @param {object} status
 * @param {() => void} _onRefresh
 */
export function renderCalendar(app, status, _onRefresh) {
  const planned = plannedMinutes(status);
  const confirmed = planConfirmedToday(status.softland, status.loop);
  app.innerHTML = `
    <div class="card">
      <h2 style="margin-top:0">Calendar</h2>
      <p class="muted">Day/week grid lands in Phase 1. Use Plan for quick-add blocks so Confirm can pass.</p>
      <p>Today non-free planned: <strong>${planned}</strong> min · Confirmed: <strong>${confirmed ? "yes" : "no"}</strong></p>
      <div class="row" style="margin-top:1rem">
        <button type="button" class="btn primary" id="btn-plan">Open Plan</button>
      </div>
    </div>
  `;
  document.getElementById("btn-plan").onclick = () => navigate("plan");
}
