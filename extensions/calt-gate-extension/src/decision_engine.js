/**
 * Signals → risk score 0–100 → ALLOW | WATCH | LOCK.
 * Hard overrides beat score. Quinn never overrides hard signals.
 */
import { GATE_V2 } from "./gate_config.js";
import { hostTier } from "./host_intel.js";
import {
  urlHasAdultTokens,
  webSearchQuery,
  isRecoverySearchQuery,
  isImageSearchUrl,
  adultTokensInText,
} from "./url_tokens.js";

/**
 * @param {object} input
 * @param {string} input.url
 * @param {string} [input.title]
 * @param {string} [input.mode] free|study|bible|planning
 * @param {boolean} [input.softlandEnabled]
 * @param {string} [input.nativeReason] morning_bible|morning_plan|…
 * @param {number} [input.contentScore]
 * @param {string} [input.contentAction] warn|lock|…
 * @param {boolean} [input.rta]
 * @param {boolean} [input.metaAdult]
 * @param {object} [input.images] { maxPornHentai, count035, maxSexy, scannerOffline }
 * @param {object} [input.quinn] { allow: boolean }
 * @param {boolean} [input.appealGrant]
 * @param {number} [input.priorScore] monotonic floor within nav
 */
export function decide(input) {
  const cfg = GATE_V2;
  const w = cfg.weights;
  const reasons = [];
  let score = Math.max(0, Number(input.priorScore) || 0);
  let hard = null;

  const url = String(input.url || "");
  const title = String(input.title || "");
  let host = "";
  try {
    host = new URL(url).hostname.toLowerCase().replace(/^www\./, "");
  } catch (e) {
    host = "";
  }

  const mode = String(input.mode || "free").toLowerCase();
  const softlandOn = input.softlandEnabled !== false;
  const tier = hostTier(host);
  reasons.push("tier:" + tier);

  // Native morning / incubation — never fight SoftLand SoT
  const nr = String(input.nativeReason || "").toLowerCase();
  if (nr.indexOf("morning") >= 0 || nr === "bible" || nr === "plan" || nr === "morning_bible" || nr === "morning_plan") {
    hard = "native_" + (nr || "morning");
  }
  if (nr === "incubation") hard = "incubation";

  if (input.appealGrant) {
    return finalize("ALLOW", score, reasons.concat(["appeal_grant"]), hard, tier, host);
  }

  if (tier === "PORN_STRICT") {
    hard = "porn_strict";
  }

  // SENSITIVE: locked while SoftLand on (any mode)
  if (tier === "SENSITIVE" && softlandOn) {
    hard = "sensitive";
  }

  // WATCH/SOCIAL in study-like modes
  const strictMode = mode === "study" || mode === "bible" || mode === "planning";
  if (softlandOn && strictMode && (tier === "WATCH" || tier === "SOCIAL")) {
    hard = "watch_study";
  }

  // L2 search query gate
  const q = webSearchQuery(url);
  if (q !== null) {
    const adultQ = adultTokensInText(q);
    if (adultQ.length && !isRecoverySearchQuery(q)) {
      hard = "adult_search_query";
      reasons.push("query:" + adultQ.slice(0, 3).join(","));
    } else if (adultQ.length && isRecoverySearchQuery(q)) {
      reasons.push("recovery_query");
    }
  }

  // Adult URL tokens — score, not hard (except already handled search)
  const trustedGrace = tier === "TRUSTED" || tier === "SEARCH";
  if (!trustedGrace && urlHasAdultTokens(url, title)) {
    score += w.adultUrlTokens;
    reasons.push("url_adult_tokens:+" + w.adultUrlTokens);
  }

  if (input.rta && tier !== "TRUSTED") {
    hard = hard || "rta";
    reasons.push("rta");
  }
  if (input.metaAdult && tier !== "TRUSTED") {
    score += w.metaAdult;
    reasons.push("meta_adult:+" + w.metaAdult);
  }

  // content_score — grace: TRUSTED never locks on keywords alone.
  // Floors match gate_policy keyword thresholds (image NSFW paused).
  const cs = Number(input.contentScore) || 0;
  const ca = String(input.contentAction || "");
  if (tier !== "TRUSTED") {
    if (ca === "lock" || cs >= 24) {
      score += w.contentLock;
      reasons.push("content_lock:+" + w.contentLock);
    } else if (ca === "warn" || cs >= 14) {
      score += w.contentWarn;
      reasons.push("content_warn:+" + w.contentWarn);
    }
  } else {
    reasons.push("trusted_skip_content");
  }

  if (tier === "UGC" || tier === "UNKNOWN" || tier === "SOCIAL") {
    score += w.ugcUnknownBaseline;
    reasons.push("ugc_baseline:+" + w.ugcUnknownBaseline);
  }

  if (isImageSearchUrl(url)) {
    score = Math.max(score, cfg.score.allowBelow);
    reasons.push("image_search_watch_floor");
  }

  const img = input.images || {};
  const imageOn = cfg.image && cfg.image.enabled !== false;
  const ph = imageOn ? Number(img.maxPornHentai) || 0 : 0;
  const c035 = imageOn ? Number(img.count035) || 0 : 0;
  const sexy = imageOn ? Number(img.maxSexy) || 0 : 0;
  if (imageOn && ph >= cfg.image.pornHentaiLock) {
    score += w.imagePornHentai06;
    reasons.push("image_porn:+" + w.imagePornHentai06);
  } else if (imageOn && c035 >= cfg.image.aggregateCount) {
    score += w.imageAggregate035;
    reasons.push("image_aggregate:+" + w.imageAggregate035);
  } else if (imageOn && sexy >= cfg.image.sexyOnly) {
    score += w.sexyOnly07;
    reasons.push("image_sexy:+" + w.sexyOnly07);
  }
  if (imageOn && img.scannerOffline && (tier === "UGC" || tier === "UNKNOWN" || tier === "SOCIAL")) {
    score = Math.max(score, cfg.score.allowBelow);
    reasons.push("scanner_offline_watch");
  }
  if (!imageOn) reasons.push("image_nsfw_paused");

  const hasHardImage =
    imageOn && (ph >= cfg.image.pornHentaiLock || c035 >= cfg.image.aggregateCount);
  const quinn = input.quinn;
  if (
    quinn &&
    quinn.allow &&
    !hard &&
    !hasHardImage &&
    !input.rta &&
    score >= cfg.score.allowBelow &&
    score < cfg.score.watchBelow
  ) {
    score = Math.max(0, score + w.quinnAllowMax);
    reasons.push("quinn_allow:" + w.quinnAllowMax);
  }

  score = Math.min(100, Math.max(0, score));

  let verdict = "ALLOW";
  if (hard) verdict = "LOCK";
  else if (score >= cfg.score.watchBelow) verdict = "LOCK";
  else if (score >= cfg.score.allowBelow) verdict = "WATCH";

  // FREE + SoftLand: WATCH tier entertainment open but SOCIAL/UGC stay scannable (WATCH verdict min)
  if (
    !hard &&
    softlandOn &&
    mode === "free" &&
    (tier === "SOCIAL" || tier === "UGC" || tier === "SENSITIVE") &&
    verdict === "ALLOW"
  ) {
    verdict = "WATCH";
    reasons.push("free_social_watch");
  }

  return finalize(verdict, score, reasons, hard, tier, host);
}

function finalize(verdict, score, reasons, hard, tier, host) {
  return {
    verdict,
    score,
    reasons,
    hard: hard || null,
    tier,
    host,
    ts: Date.now(),
  };
}

export function mergeMonotonic(prev, next) {
  if (!prev) return next;
  // Different host = new decision; do not inherit LOCK/hard from the previous site.
  const prevHost = String(prev.host || "").toLowerCase();
  const nextHost = String(next.host || "").toLowerCase();
  if (prevHost && nextHost && prevHost !== nextHost) return next;
  const score = Math.max(prev.score || 0, next.score || 0);
  const hard = next.hard || prev.hard || null;
  let verdict = next.verdict;
  if (prev.verdict === "LOCK" || hard) verdict = "LOCK";
  else if (verdict === "ALLOW" && (prev.verdict === "WATCH" || score >= GATE_V2.score.allowBelow)) {
    verdict = score >= GATE_V2.score.watchBelow ? "LOCK" : "WATCH";
  }
  return {
    ...next,
    score,
    hard,
    verdict,
    reasons: (prev.reasons || []).concat(next.reasons || []).slice(-40),
  };
}
