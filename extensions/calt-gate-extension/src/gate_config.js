/**
 * CALT Gate v2 — thresholds and weights (single place to tune).
 * Strict on tubes/UGC; grace for TRUSTED chats and recovery searches.
 *
 * Defaults are intentionally higher so normal browsing stays ALLOW;
 * scores ≥ highScoreLogAt are saved with timestamps for review.
 */
export const GATE_V2_DEFAULTS = {
  enabled: true,
  shadowOnly: false,
  version: "2.0.1",
  score: {
    /** under this → ALLOW */
    allowBelow: 50,
    /** allowBelow..watchBelow-1 → WATCH; watchBelow+ or hard → LOCK */
    watchBelow: 80,
    /** persist every verdict with score ≥ this (normal-usage telemetry) */
    highScoreLogAt: 50,
  },
  weights: {
    adultUrlTokens: 35,
    contentWarn: 15,
    contentLock: 35,
    imagePornHentai06: 50,
    imageAggregate035: 40,
    sexyOnly07: 12,
    ugcUnknownBaseline: 8,
    quinnAllowMax: -15,
    metaAdult: 45,
  },
  image: {
    /** NSFW WASM image locks paused — keyword SoftLand only until re-tuned. */
    enabled: false,
    pornHentaiLock: 0.92,
    aggregateMin: 0.85,
    aggregateCount: 8,
    sexyOnly: 0.95,
    minPx: 150,
    maxConcurrent: 1,
    cacheMax: 400,
  },
  appeal: {
    cooldownMs: 15 * 60 * 1000,
    grantMs: 30 * 60 * 1000,
    maxGrantsPerDay: 6,
  },
  blocklist: {
    alarmMinutes: 60 * 24 * 7,
    storageKey: "gateBlocklistV2",
    maxHosts: 350000,
  },
  log: {
    maxEntries: 500,
    storageKey: "gateVerdictLogV2",
    highScoreKey: "gateHighScoreLogV2",
    highScoreMax: 300,
    thresholdsKey: "gateThresholdsV2",
  },
  looseningDelayMs: 24 * 60 * 60 * 1000,
};

/** Mutable runtime config (defaults + saved threshold overrides). */
export const GATE_V2 = JSON.parse(JSON.stringify(GATE_V2_DEFAULTS));

export function snapshotThresholds() {
  return {
    allowBelow: GATE_V2.score.allowBelow,
    watchBelow: GATE_V2.score.watchBelow,
    highScoreLogAt: GATE_V2.score.highScoreLogAt,
    imagePornHentaiLock: GATE_V2.image.pornHentaiLock,
    aggregateMin: GATE_V2.image.aggregateMin,
    aggregateCount: GATE_V2.image.aggregateCount,
    sexyOnly: GATE_V2.image.sexyOnly,
    savedAt: Date.now(),
  };
}

export function applyThresholdOverrides(partial) {
  if (!partial || typeof partial !== "object") return snapshotThresholds();
  const n = (v, d) => {
    const x = Number(v);
    return Number.isFinite(x) ? x : d;
  };
  GATE_V2.score.allowBelow = n(partial.allowBelow, GATE_V2.score.allowBelow);
  GATE_V2.score.watchBelow = n(partial.watchBelow, GATE_V2.score.watchBelow);
  GATE_V2.score.highScoreLogAt = n(partial.highScoreLogAt, GATE_V2.score.highScoreLogAt);
  if (GATE_V2.score.allowBelow >= GATE_V2.score.watchBelow) {
    GATE_V2.score.watchBelow = GATE_V2.score.allowBelow + 20;
  }
  if (partial.imagePornHentaiLock != null) {
    GATE_V2.image.pornHentaiLock = n(partial.imagePornHentaiLock, GATE_V2.image.pornHentaiLock);
  }
  if (partial.aggregateMin != null) {
    GATE_V2.image.aggregateMin = n(partial.aggregateMin, GATE_V2.image.aggregateMin);
  }
  if (partial.aggregateCount != null) {
    GATE_V2.image.aggregateCount = n(partial.aggregateCount, GATE_V2.image.aggregateCount);
  }
  if (partial.sexyOnly != null) {
    GATE_V2.image.sexyOnly = n(partial.sexyOnly, GATE_V2.image.sexyOnly);
  }
  return snapshotThresholds();
}

export async function loadSavedThresholds(storage) {
  if (!storage || !storage.local) return snapshotThresholds();
  try {
    const key = GATE_V2.log.thresholdsKey;
    const r = await storage.local.get(key);
    if (r && r[key]) applyThresholdOverrides(r[key]);
  } catch (e) {
    /* ignore */
  }
  return snapshotThresholds();
}

export async function saveThresholds(storage, partial) {
  const snap = applyThresholdOverrides(partial || snapshotThresholds());
  if (storage && storage.local) {
    try {
      await storage.local.set({ [GATE_V2.log.thresholdsKey]: snap });
    } catch (e) {
      /* ignore */
    }
  }
  return snap;
}

export const RECOVERY_SEARCH_RE =
  /\bbreast\s+cancer\b|\bsex\s+education\b|\b(quit|quitting|stop|stopping|block|blocking|addiction|filter|filters|recovery|recover|help|helpline|education|educational|cancer|health|therapy|religious|bible)\b.{0,40}\b(porn|pornography|sex|nsfw|xxx|hentai)\b|\b(porn|pornography|sex)\b.{0,40}\b(addiction|block|blocking|quit|education|cancer|health|filter)\b/i;
