import { bibleDoneToday, localYmd } from "./mirrors.js";

export function planConfirmedToday(softland, loop) {
  if (loop?.plan_confirmed) return true;
  const goals = softland?.goals;
  if (!goals?.plan_confirmed) return false;
  return goals.plan_confirmed_for_date === localYmd();
}

export function plannedMinutes(status) {
  const n = status?.loop?.planned_minutes;
  return typeof n === "number" && n > 0 ? n : 0;
}

/** Backend Confirm requires bible_done today + ≥1 non-free planned minute. */
export function canConfirmPlan(status) {
  const bible = bibleDoneToday(status.softland, status.loop);
  const mins = plannedMinutes(status);
  return { bible, mins, ok: bible && mins > 0 };
}

export function confirmErrorMessage(code) {
  if (code === "bible_required") {
    return "Bible required — mark today’s morning chapter done first.";
  }
  if (code === "plan_required") {
    return "Plan required — add at least one non-free block for today (Plan tab), then Confirm.";
  }
  if (code === "store_load_failed") {
    return "Enforcer store unavailable — is CALTEnforcer running?";
  }
  return code || "Confirm failed";
}

export function isoLocal(d = new Date()) {
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  const hh = String(d.getHours()).padStart(2, "0");
  const mm = String(d.getMinutes()).padStart(2, "0");
  const ss = String(d.getSeconds()).padStart(2, "0");
  return `${y}-${m}-${day}T${hh}:${mm}:${ss}`;
}

/** Snap to next :00 / :30 for quick-add start. */
export function nextHalfHourIso() {
  const d = new Date();
  d.setSeconds(0, 0);
  const mins = d.getMinutes();
  if (mins === 0 || mins === 30) {
    d.setMinutes(mins + 30);
  } else if (mins < 30) {
    d.setMinutes(30);
  } else {
    d.setHours(d.getHours() + 1, 0, 0, 0);
  }
  return isoLocal(d);
}
