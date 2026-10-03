import { enforcerCmd } from "../bridge.js";
import { bibleDoneToday } from "../mirrors.js";
import { currentRoute, navigate } from "../router.js";
import {
  canConfirmPlan,
  confirmErrorMessage,
  nextHalfHourIso,
  planConfirmedToday,
  plannedMinutes,
} from "../day-loop.js";

/**
 * Morning ritual overlay:
 *   1) Bible until bible_done_for_date == today
 *   2) Plan until plan_confirmed_for_date == today (Confirm = SoftLand + Arm)
 *
 * SoftLand host gate (morning_bible / morning_plan) is enforcer-side; this UI
 * must not disappear before Confirm just because Bible is done.
 *
 * @param {HTMLElement} morningEl
 * @param {object} status
 * @param {() => void} onRefresh
 */
export function renderMorning(morningEl, status, onRefresh) {
  const route = currentRoute();
  const hasSignal = Boolean(status?.softland?.goals || status?.loop);
  const bedtime = Boolean(status?.loop?.bedtime_active);
  const bible = bibleDoneToday(status.softland, status.loop);
  const planOk = planConfirmedToday(status.softland, status.loop);

  if (bedtime || planOk) {
    morningEl.classList.add("hidden");
    morningEl.innerHTML = "";
    return;
  }

  // Fail closed: if mirrors/pipe are empty, keep a gate so ritual isn't skipped.
  if (!hasSignal) {
    morningEl.classList.remove("hidden");
    morningEl.innerHTML = `
      <h1>Morning gate</h1>
      <p>Can’t read SoftLand / day-loop status. Start CALTEnforcer, then retry.</p>
      <p class="error" id="morning-err" hidden></p>
      <div class="row">
        <button type="button" class="btn primary" id="btn-retry">Retry</button>
      </div>
    `;
    document.getElementById("btn-retry").onclick = () => onRefresh();
    return;
  }

  // Let user work on Bible / Plan pages without the fullscreen covering them.
  if (!bible && route === "bible") {
    morningEl.classList.add("hidden");
    return;
  }
  if (bible && !planOk && (route === "plan" || route === "calendar")) {
    morningEl.classList.add("hidden");
    return;
  }

  morningEl.classList.remove("hidden");

  if (!bible) {
    morningEl.innerHTML = `
      <h1>Morning chapter</h1>
      <p>Reading alone doesn’t unlock the day. Mark the chapter done, then plan and Confirm — SoftLand stays on for the host gate; Arm arms only after Confirm.</p>
      <p class="error" id="morning-err" hidden></p>
      <div class="row">
        <button type="button" class="btn" id="btn-open-bible">Open Bible</button>
        <button type="button" class="btn primary" id="btn-mark-done">Mark chapter done</button>
      </div>
    `;
    document.getElementById("btn-open-bible").onclick = () => navigate("bible");
    document.getElementById("btn-mark-done").onclick = async () => {
      const btn = document.getElementById("btn-mark-done");
      const err = document.getElementById("morning-err");
      btn.disabled = true;
      err.hidden = true;
      const r = await enforcerCmd(
        "bible.devotion.done",
        { slot: "morning", done: true },
        5000,
      );
      if (!r || r.ok === false || r.error) {
        err.textContent = r?.error || "Mark failed — is calt_enforcer running?";
        err.hidden = false;
        btn.disabled = false;
        return;
      }
      navigate("plan");
      onRefresh();
    };
    return;
  }

  // Bible done, plan not confirmed → plan gate
  const mins = plannedMinutes(status);
  const gate = canConfirmPlan(status);
  morningEl.innerHTML = `
    <h1>Confirm today’s plan</h1>
    <p>Bible is done. Add at least one <strong>non-free</strong> block for today, then Confirm. That turns SoftLand + Arm on together.</p>
    <p>Planned non-free minutes today: <strong id="m-planned">${mins}</strong></p>
    <p class="error" id="morning-err" hidden></p>
    <div class="row">
      <button type="button" class="btn" id="btn-open-plan">Open Plan</button>
      <button type="button" class="btn" id="btn-quick-add">+ 60m study</button>
      <button type="button" class="btn primary" id="btn-confirm" ${gate.ok ? "" : "disabled"}>Confirm plan</button>
    </div>
  `;

  document.getElementById("btn-open-plan").onclick = () => navigate("plan");

  document.getElementById("btn-quick-add").onclick = async () => {
    const btn = document.getElementById("btn-quick-add");
    const err = document.getElementById("morning-err");
    btn.disabled = true;
    err.hidden = true;
    const r = await enforcerCmd(
      "plan.upsert",
      {
        title: "Deep work",
        category: "study",
        start_at: nextHalfHourIso(),
        duration_minutes: 60,
      },
      6000,
    );
    if (!r || r.ok === false || r.error) {
      err.textContent = r?.error || "Could not add block";
      err.hidden = false;
      btn.disabled = false;
      return;
    }
    onRefresh();
  };

  const conf = document.getElementById("btn-confirm");
  conf.onclick = async () => {
    const err = document.getElementById("morning-err");
    conf.disabled = true;
    err.hidden = true;
    const r = await enforcerCmd("day.confirm_plan", {}, 8000);
    if (!r || r.ok === false || r.error) {
      err.textContent = confirmErrorMessage(r?.error);
      err.hidden = false;
      conf.disabled = false;
      return;
    }
    navigate("home");
    onRefresh();
  };
}
