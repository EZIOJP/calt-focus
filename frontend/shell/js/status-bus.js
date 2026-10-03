/** Mirrors first; native status_tick for SoftLand; rare pipe for day.loop_snapshot. */

import { enforcerCmd, bridgeAvailable } from "./bridge.js";
import { fetchJson, localYmd } from "./mirrors.js";

let cachedSoftland = null;
let cachedRollup = null;
let cachedLoop = null;
let lastPipeAt = 0;
const PIPE_MIN_MS = 30_000;

function syncLoopFromSoftland(softland) {
  if (!softland?.goals) return;
  const today = localYmd();
  const g = softland.goals;
  const bible =
    Boolean(g.bible_done) && g.bible_done_for_date === today;
  const plan =
    Boolean(g.plan_confirmed) && g.plan_confirmed_for_date === today;
  if (!cachedLoop) {
    cachedLoop = {
      bible_done: bible,
      plan_confirmed: plan,
      planned_minutes: 0,
      bedtime_active: false,
    };
    return;
  }
  cachedLoop = {
    ...cachedLoop,
    bible_done: bible,
    plan_confirmed: plan,
  };
}

export async function loadStatus({ forcePipe = false } = {}) {
  const [softland, rollup] = await Promise.all([
    fetchJson("softland_policy.json"),
    fetchJson("day_rollup.json"),
  ]);
  if (softland) {
    cachedSoftland = softland;
    syncLoopFromSoftland(softland);
  } else if (cachedSoftland) {
    syncLoopFromSoftland(cachedSoftland);
  }
  if (rollup) cachedRollup = rollup;

  const now = Date.now();
  const needPipe =
    bridgeAvailable() &&
    (forcePipe || !cachedLoop || now - lastPipeAt >= PIPE_MIN_MS);
  if (needPipe) {
    const r = await enforcerCmd("day.loop_snapshot", {}, 2500);
    lastPipeAt = now;
    if (r && r.ok !== false && r.loop) {
      cachedLoop = r.loop;
      syncLoopFromSoftland(cachedSoftland || softland);
    }
  }

  return {
    softland: cachedSoftland || softland,
    rollup: cachedRollup || rollup,
    loop: cachedLoop,
  };
}

/** Apply native PushStatusTick; ignore keepalive. Returns true if SoftLand changed. */
export function applyStatusTick(msg) {
  if (!msg || msg.type !== "status_tick" || msg.keepalive) return false;
  if (msg.softland && typeof msg.softland === "object") {
    const prev = cachedSoftland ? JSON.stringify(cachedSoftland.goals) : "";
    cachedSoftland = msg.softland;
    syncLoopFromSoftland(msg.softland);
    const next = JSON.stringify(msg.softland.goals || null);
    return prev !== next;
  }
  return false;
}

export function installStatusPushListener(onSoftlandChange) {
  if (!window.chrome?.webview?.addEventListener) return () => {};
  const onMsg = (e) => {
    let data = e.data;
    if (typeof data === "string") {
      try {
        data = JSON.parse(data);
      } catch {
        return;
      }
    }
    if (applyStatusTick(data)) onSoftlandChange?.();
  };
  window.chrome.webview.addEventListener("message", onMsg);
  return () => window.chrome.webview.removeEventListener("message", onMsg);
}
