import { GATE_V2, snapshotThresholds } from "./gate_config.js";

const mem = [];
const highMem = [];

function iso(ts) {
  try {
    return new Date(ts).toISOString();
  } catch (e) {
    return String(ts);
  }
}

function localTime(ts) {
  try {
    return new Date(ts).toLocaleString();
  } catch (e) {
    return iso(ts);
  }
}

export function pushVerdict(entry) {
  const ts = entry.ts || Date.now();
  const row = {
    ts,
    iso: iso(ts),
    local: localTime(ts),
    host: entry.host || "",
    verdict: entry.verdict || "",
    score: Number(entry.score) || 0,
    reasons: (entry.reasons || []).slice(0, 24),
    hard: entry.hard || null,
    tier: entry.tier || "",
    mode: entry.mode || "",
    thresholds: entry.thresholds || snapshotThresholds(),
  };
  mem.unshift(row);
  while (mem.length > GATE_V2.log.maxEntries) mem.pop();

  const floor = GATE_V2.score.highScoreLogAt;
  if (row.score >= floor) {
    pushHighScore(row);
  }
  return row;
}

function pushHighScore(row) {
  const hi = {
    ts: row.ts,
    iso: row.iso,
    local: row.local,
    host: row.host,
    verdict: row.verdict,
    score: row.score,
    tier: row.tier,
    mode: row.mode,
    hard: row.hard,
    reasons: row.reasons,
    allowBelow: row.thresholds && row.thresholds.allowBelow,
    watchBelow: row.thresholds && row.thresholds.watchBelow,
  };
  highMem.unshift(hi);
  while (highMem.length > GATE_V2.log.highScoreMax) highMem.pop();
}

export function recentVerdicts(n) {
  return mem.slice(0, Math.max(1, n || 100));
}

export function recentHighScores(n) {
  return highMem.slice(0, Math.max(1, n || 100));
}

export async function persistVerdicts(storage) {
  if (!storage || !storage.local) return;
  try {
    await storage.local.set({
      [GATE_V2.log.storageKey]: mem.slice(0, 200),
      [GATE_V2.log.highScoreKey]: highMem.slice(0, GATE_V2.log.highScoreMax),
      [GATE_V2.log.thresholdsKey]: snapshotThresholds(),
    });
  } catch (e) {
    /* ignore */
  }
}

export async function loadVerdicts(storage) {
  if (!storage || !storage.local) return;
  try {
    const r = await storage.local.get([
      GATE_V2.log.storageKey,
      GATE_V2.log.highScoreKey,
    ]);
    const list = r && r[GATE_V2.log.storageKey];
    if (Array.isArray(list)) {
      mem.length = 0;
      for (let i = 0; i < list.length && i < GATE_V2.log.maxEntries; i++) mem.push(list[i]);
    }
    const hi = r && r[GATE_V2.log.highScoreKey];
    if (Array.isArray(hi)) {
      highMem.length = 0;
      for (let i = 0; i < hi.length && i < GATE_V2.log.highScoreMax; i++) highMem.push(hi[i]);
    }
  } catch (e) {
    /* ignore */
  }
}
