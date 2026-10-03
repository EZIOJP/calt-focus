/**
 * Shared distraction-gate helpers for Chromium + Firefox SelfTracker.
 * Edge-only SelfTracker gate policy (Chromium MV3).
 * Server `browser` payload is source of truth; seeds below are offline fallback.
 *
 * Light routine: keyword = string match on URL + title only (no DOM crawl).
 * Gate refresh ~4s when browsing; 1 min idle alarm backup.
 */
/* eslint-disable no-unused-vars */

var GATE_API_URL = "http://127.0.0.1:8000/api/behavior/distraction-gate";
var GATE_ALERT_URL = "http://127.0.0.1:8000/api/behavior/gate-alert";
var GATE_EXT_LOG_URL = "http://127.0.0.1:8000/api/behavior/gate-extension-log";
var CALT_TAB_CMD_URL = "http://127.0.0.1:8000/api/behavior/calt-tab-command";
var GATE_NOTIFY_WS_URL = "ws://127.0.0.1:8000/ws/gate";
var CALT_BIBLE_URL = "http://localhost:5173/bible";
var CALT_PRODUCTIVITY_URL = "http://localhost:5173/productivity?tab=plan";
var CALT_ORIGIN = "http://localhost:5173";
/** Suggested heartbeat while tabs are active (seconds). Server may override via intervals. */
var GATE_POLL_ACTIVE_S = 300;
/** Min gap between gate-alert POSTs (ms). */
var GATE_ALERT_GAP_MS = 45000;

// BEGIN GENERATED_GATE_SEEDS (scripts/sync_gate_policy_seeds.py)
/** Offline seeds — generated from backend.behavior.browser_gate_policy. Do not edit by hand. */
/** Shopping / house / errands — allowed in free mode (and errands-lite). */
var FREE_LIFE_ALLOW_DOMAINS = [
  "amazon.com",
  "amazon.in",
  "flipkart.com",
  "myntra.com",
  "ajio.com",
  "bigbasket.com",
  "blinkit.com",
  "swiggy.com",
  "zomato.com",
  "ikea.com",
  "housing.com",
  "magicbricks.com",
  "nobroker.in",
  "99acres.com",
  "nykaa.com",
  "meesho.com",
];

/** @deprecated Prefer gateCache.browser.watch_domains — offline fallback. */
var DISTRACTION_DOMAINS = [
  "youtube.com",
  "youtu.be",
  "netflix.com",
  "primevideo.com",
  "hotstar.com",
  "disneyplus.com",
  "hulu.com",
  "twitch.tv",
  "crunchyroll.com",
  "sonyliv.com",
  "zee5.com",
];

var FALLBACK_ALLOW_DOMAINS = [
  "localhost",
  "127.0.0.1",
  "colab.research.google.com",
  "scaler.com",
  "interviewbit.com",
  "scaleracademy.com",
  "scaler-production-new.s3.ap-southeast-1.amazonaws.com",
  "github.com",
  "githubusercontent.com",
  "gitlab.com",
  "stackoverflow.com",
  "stackexchange.com",
  "docs.google.com",
  "drive.google.com",
  "drive.usercontent.google.com",
  "sheets.google.com",
  "slides.google.com",
  "meet.google.com",
  "classroom.google.com",
  "accounts.google.com",
  "myaccount.google.com",
  "gemini.google.com",
  "aistudio.google.com",
  "bard.google.com",
  "googleusercontent.com",
  "google.com",
  "bing.com",
  "duckduckgo.com",
  "search.brave.com",
  "ntp.msn.com",
  "msn.com",
  "notion.so",
  "notion.site",
  "leetcode.com",
  "codeforces.com",
  "atcoder.jp",
  "hackerrank.com",
  "coursera.org",
  "udemy.com",
  "edx.org",
  "khanacademy.org",
  "brilliant.org",
  "arxiv.org",
  "wikipedia.org",
  "developer.mozilla.org",
  "mdn.io",
  "python.org",
  "docs.python.org",
  "pypi.org",
  "npmjs.com",
  "vscode.dev",
  "cursor.com",
  "chatgpt.com",
  "claude.ai",
  "openai.com",
  "anthropic.com",
  "figma.com",
  "excalidraw.com",
  "obsidian.md",
  "zoom.us",
  "web.whatsapp.com",
  "numpy.org",
  "pandas.pydata.org",
  "scipy.org",
  "scikit-learn.org",
  "matplotlib.org",
  "seaborn.pydata.org",
  "plotly.com",
  "pytorch.org",
  "tensorflow.org",
  "keras.io",
  "huggingface.co",
  "jax.dev",
  "readthedocs.io",
  "polars.tech",
  "realpython.com",
  "pydata.org",
  "kaggle.com",
  "datacamp.com",
  "towardsdatascience.com",
  "medium.com",
  "fast.ai",
  "course.fast.ai",
  "deeplearning.ai",
  "paperswithcode.com",
  "distill.pub",
  "ocw.mit.edu",
  "cs231n.stanford.edu",
  "statisticsbyjim.com",
  "paperspace.com",
  "deepnote.com",
  "databricks.com",
  "stats.stackexchange.com",
  "datascience.stackexchange.com",
];

var FALLBACK_WATCH_DOMAINS = [
  "youtube.com",
  "youtu.be",
  "netflix.com",
  "primevideo.com",
  "hotstar.com",
  "disneyplus.com",
  "hulu.com",
  "twitch.tv",
  "crunchyroll.com",
  "sonyliv.com",
  "zee5.com",
];

/** Always blocked in bible / planning / study — even if server flags are stale. */
var FORCE_WATCH_HOSTS = [
  "youtube.com",
  "youtu.be",
  "netflix.com",
  "primevideo.com",
  "hotstar.com",
  "disneyplus.com",
  "hulu.com",
  "twitch.tv",
  "crunchyroll.com",
  "sonyliv.com",
  "zee5.com",
];
var STRICT_DAY_MODES = ["bible", "planning", "study"];

/** Always blocked (every mode including free) when offline / stale. */
var FORCE_PORN_HOSTS = [
  "pornhub.com",
  "xvideos.com",
  "xnxx.com",
  "xhamster.com",
  "redtube.com",
  "youporn.com",
  "tube8.com",
  "spankbang.com",
  "chaturbate.com",
  "onlyfans.com",
  "porn.com",
  "sex.com",
  "hentaihaven.xxx",
  "nhentai.net",
  "rule34.xxx",
  "erome.com",
  "v3.erome.com",
  "eromecdn.com",
  "eporner.com",
  "hqporner.com",
  "porntrex.com",
  "beeg.com",
  "txxx.com",
  "redgifs.com",
  "imagefap.com",
  "motherless.com",
  "fapello.com",
  "missav.com",
  "jable.tv",
  "thisvid.com",
  "xhamster2.com",
  "xhamster3.com",
  "xvideos2.com",
  "xnxx.tv",
  "pornhub.org",
  "pornhub.net",
];
var FORCE_PORN_SUFFIXES = [
  ".xxx",
  ".adult",
  ".porn",
  ".sex",
];
var FALLBACK_PORN_DOMAINS = FORCE_PORN_HOSTS.slice();
var FALLBACK_PORN_SUFFIXES = FORCE_PORN_SUFFIXES.slice();

var FALLBACK_SOCIAL_DOMAINS = [
  "instagram.com",
  "reddit.com",
  "x.com",
  "twitter.com",
  "tiktok.com",
  "facebook.com",
];

/** Temporary per-host soft-allow from locked.html (extension chrome.storage.local). */
var TEMP_ALLOW_MS = 60000;
var TEMP_ALLOW_STORAGE_KEY = "tempAllows";

/** Offline keyword seed — prefer server block_keywords_list. */
var FALLBACK_BLOCK_KEYWORDS = [
  "bdsm",
  "porn",
  "porno",
  "pornography",
  "xxx",
  "onlyfans",
  "fansly",
  "manyvids",
  "chaturbate",
  "stripchat",
  "hentai",
  "nsfw",
  "nude",
  "nudes",
  "nudity",
  "fetish",
  "bondage",
  "pegging",
  "blowjob",
  "handjob",
  "deepthroat",
  "cumshot",
  "creampie",
  "gangbang",
  "threesome",
  "milf",
  "incest",
  "rule34",
  "rule 34",
  "xhamster",
  "xvideos",
  "pornhub",
  "redtube",
  "youporn",
  "spankbang",
  "erome",
  "eporner",
  "hqporner",
  "redgifs",
  "camgirl",
  "cam girl",
  "sex cam",
  "live sex",
  "sex tape",
  "sex video",
  "adult video",
  "erotic",
  "erotica",
  "hardcore porn",
  "naked photo",
  "naked pics",
  "nude photo",
  "nude pics",
  "xxx video",
];
// END GENERATED_GATE_SEEDS
function hostnameFromUrl(url) {
  try {
    return new URL(url).hostname.toLowerCase().replace(/^www\./, "");
  } catch (e) {
    return "";
  }
}

function hostMatchesDomain(host, domain) {
  var h = String(host || "")
    .toLowerCase()
    .replace(/^www\./, "");
  var d = String(domain || "")
    .toLowerCase()
    .replace(/^www\./, "");
  if (!h || !d) return false;
  return h === d || h.endsWith("." + d);
}

/** Scaler lecture attachment buckets on S3 — not all of amazonaws.com. */
function isScalerAttachmentHost(host) {
  var h = String(host || "")
    .toLowerCase()
    .replace(/^www\./, "");
  if (!h.endsWith(".amazonaws.com")) return false;
  if (h.indexOf(".s3.") < 0) return false;
  var bucket = h.split(".")[0] || "";
  return bucket.indexOf("scaler") === 0;
}

function listMatch(host, domains) {
  if (!domains || !domains.length) return false;
  for (var i = 0; i < domains.length; i++) {
    if (hostMatchesDomain(host, domains[i])) return true;
  }
  return false;
}

function isStrictDayMode(mode) {
  return STRICT_DAY_MODES.indexOf(String(mode || "").toLowerCase()) >= 0;
}

/** Prefer live server force/watch lists; offline seeds are last resort. */
function forceWatchList(policy) {
  if (policy && policy.force_watch_hosts && policy.force_watch_hosts.length) {
    return policy.force_watch_hosts;
  }
  if (policy && policy.watch_domains && policy.watch_domains.length) {
    return policy.watch_domains;
  }
  return FORCE_WATCH_HOSTS;
}

function forcePornList(policy) {
  if (policy && policy.force_porn_hosts && policy.force_porn_hosts.length) {
    return policy.force_porn_hosts;
  }
  if (policy && policy.porn_domains && policy.porn_domains.length) {
    return policy.porn_domains;
  }
  return FORCE_PORN_HOSTS;
}

function forcePornSuffixes(policy) {
  if (policy && policy.porn_suffixes && policy.porn_suffixes.length) {
    return policy.porn_suffixes;
  }
  return FORCE_PORN_SUFFIXES;
}

function isForceWatchHost(host, policy) {
  return listMatch(host, forceWatchList(policy || null));
}

function isForcePornHost(host, policy) {
  var h = String(host || "")
    .toLowerCase()
    .replace(/^www\./, "");
  if (!h) return false;
  if (listMatch(h, forcePornList(policy || null))) return true;
  var suffixes = forcePornSuffixes(policy || null);
  for (var i = 0; i < suffixes.length; i++) {
    if (h.endsWith(suffixes[i])) return true;
  }
  return false;
}

/** Unique host list for DNR — prefer force_* order, then remaining watch/porn. */
function dnrHostList(primary, secondary, maxHosts) {
  var out = [];
  var seen = Object.create(null);
  function pushAll(arr) {
    if (!arr || !arr.length) return;
    for (var i = 0; i < arr.length; i++) {
      var h = String(arr[i] || "")
        .toLowerCase()
        .replace(/^www\./, "");
      if (!h || seen[h]) continue;
      seen[h] = true;
      out.push(h);
      if (out.length >= maxHosts) return;
    }
  }
  pushAll(primary);
  pushAll(secondary);
  return out;
}

function normalizeHost(host) {
  return String(host || "")
    .toLowerCase()
    .replace(/^www\./, "");
}

/**
 * Watch / porn / social (and force lists) must never receive a temp allow.
 * @returns {boolean}
 */
function isTempAllowExcludedHost(host, policy) {
  var h = normalizeHost(host);
  if (!h) return true;
  var pol = policy || browserPolicyOrFallback(null);
  if (isForceWatchHost(h, pol) || isForcePornHost(h, pol)) return true;
  var cat = classifyHostCategory(h, pol);
  return cat === "watch" || cat === "porn" || cat === "social";
}

/** Drop expired entries; keep { host, until } shape. */
function pruneTempAllows(list, now) {
  var t = typeof now === "number" ? now : Date.now();
  var out = [];
  if (!list || !list.length) return out;
  for (var i = 0; i < list.length; i++) {
    var e = list[i];
    if (!e || !e.host) continue;
    var until = Number(e.until);
    if (!until || until <= t) continue;
    out.push({ host: normalizeHost(e.host), until: until });
  }
  return out;
}

/**
 * @returns {boolean} true if host has a non-expired temp allow and is not excluded.
 */
function isHostTempAllowed(host, tempAllows, now, policy) {
  var h = normalizeHost(host);
  if (!h) return false;
  if (isTempAllowExcludedHost(h, policy)) return false;
  var t = typeof now === "number" ? now : Date.now();
  var list = pruneTempAllows(tempAllows, t);
  for (var i = 0; i < list.length; i++) {
    if (hostMatchesDomain(h, list[i].host) || hostMatchesDomain(list[i].host, h)) {
      if (t < list[i].until) return true;
    }
  }
  return false;
}

function tempAllowUntilForHost(host, tempAllows, now) {
  var h = normalizeHost(host);
  if (!h) return 0;
  var t = typeof now === "number" ? now : Date.now();
  var list = pruneTempAllows(tempAllows, t);
  var best = 0;
  for (var i = 0; i < list.length; i++) {
    if (hostMatchesDomain(h, list[i].host) || hostMatchesDomain(list[i].host, h)) {
      if (list[i].until > best) best = list[i].until;
    }
  }
  return best > t ? best : 0;
}

/**
 * Decide whether locked.html may grant a temp allow for this host.
 * @returns {{ ok: boolean, error?: string, entry?: { host: string, until: number } }}
 */
function buildTempAllowGrant(host, now, policy) {
  var h = normalizeHost(host);
  var t = typeof now === "number" ? now : Date.now();
  var pol = policy || browserPolicyOrFallback(null);
  if (!h) return { ok: false, error: "No host to allow" };
  if (isTempAllowExcludedHost(h, pol)) {
    var cat = classifyHostCategory(h, pol);
    if (isForceWatchHost(h, pol) || cat === "watch") {
      return { ok: false, error: "Watch sites can't be temporarily allowed" };
    }
    if (isForcePornHost(h, pol) || cat === "porn") {
      return { ok: false, error: "Distractions can't be temporarily allowed" };
    }
    if (cat === "social") {
      return { ok: false, error: "Social sites can't be temporarily allowed" };
    }
    return { ok: false, error: "This site can't be temporarily allowed" };
  }
  return { ok: true, entry: { host: h, until: t + TEMP_ALLOW_MS } };
}

/** Upsert one grant into the list (prunes expired). */
function upsertTempAllow(list, entry, now) {
  var t = typeof now === "number" ? now : Date.now();
  var out = pruneTempAllows(list, t);
  if (!entry || !entry.host) return out;
  var h = normalizeHost(entry.host);
  var until = Number(entry.until) || 0;
  if (!h || until <= t) return out;
  var next = [];
  for (var i = 0; i < out.length; i++) {
    if (out[i].host !== h) next.push(out[i]);
  }
  next.push({ host: h, until: until });
  return next;
}

function decodeUriSafe(s) {
  try {
    return decodeURIComponent(String(s || ""));
  } catch (e) {
    return String(s || "");
  }
}

/**
 * Boundary-aware keyword match (case-insensitive). Returns matched keyword or "".
 */
function textMatchesKeywords(haystack, keywords) {
  var hay = decodeUriSafe(haystack).toLowerCase();
  if (!hay || !keywords || !keywords.length) return "";
  for (var i = 0; i < keywords.length; i++) {
    var kw = String(keywords[i] || "")
      .trim()
      .toLowerCase();
    if (!kw || kw.length < 3) continue;
    var escaped = kw.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    var re = new RegExp("(^|[^a-z0-9])" + escaped + "([^a-z0-9]|$)", "i");
    if (re.test(hay)) return kw;
  }
  return "";
}

function browserPolicyOrFallback(browser) {
  var b = browser || {};
  var mode = String(b.mode || "").toLowerCase();
  if (["bible", "planning", "study", "free"].indexOf(mode) < 0) {
    // Prefer study (watch blocked) over free when mode missing but flags look enforcing.
    mode = b.enforce || b.block_other || b.block_watch_sites ? "study" : "free";
  }
  var strict = isStrictDayMode(mode);
  var prefixes =
    b.localhost_path_prefixes && b.localhost_path_prefixes.length
      ? b.localhost_path_prefixes
      : strict
        ? ["/bible", "/productivity", "/profile", "/lecture-notes"]
        : [];
  return {
    mode: mode,
    mode_label: b.mode_label || mode.toUpperCase(),
    allow_domains: b.allow_domains && b.allow_domains.length ? b.allow_domains : FALLBACK_ALLOW_DOMAINS,
    watch_domains: b.watch_domains && b.watch_domains.length ? b.watch_domains : FALLBACK_WATCH_DOMAINS,
    porn_domains: b.porn_domains && b.porn_domains.length ? b.porn_domains : FALLBACK_PORN_DOMAINS,
    porn_suffixes: b.porn_suffixes && b.porn_suffixes.length ? b.porn_suffixes : FALLBACK_PORN_SUFFIXES,
    social_domains: b.social_domains && b.social_domains.length ? b.social_domains : FALLBACK_SOCIAL_DOMAINS,
    block_keywords_list:
      b.block_keywords_list && b.block_keywords_list.length
        ? b.block_keywords_list
        : FALLBACK_BLOCK_KEYWORDS,
    force_watch_hosts:
      b.force_watch_hosts && b.force_watch_hosts.length ? b.force_watch_hosts : FORCE_WATCH_HOSTS,
    force_porn_hosts:
      b.force_porn_hosts && b.force_porn_hosts.length ? b.force_porn_hosts : FORCE_PORN_HOSTS,
    localhost_path_prefixes: prefixes,
    // Strict day modes always block watch (YouTube etc.) — never trust a stale false flag.
    block_watch_sites: b.block_watch_sites === true || strict,
    block_porn: b.block_porn !== false,
    block_social: b.block_social === true || strict,
    block_keywords: b.block_keywords !== false,
    block_other: b.block_other === true || strict,
    strict_allowlist: b.strict_allowlist === true || mode === "bible" || mode === "planning",
    enforce: Boolean(b.enforce) || strict,
    redirect_url: b.redirect_url || null,
    redirect_reason: b.redirect_reason || null,
    bible_url: b.bible_url || CALT_BIBLE_URL,
    plan_url: b.plan_url || CALT_PRODUCTIVITY_URL,
    morning_next: b.morning_next || "open",
    allow_free_life: b.allow_free_life === true,
    free_life_allow_domains:
      b.free_life_allow_domains && b.free_life_allow_domains.length
        ? b.free_life_allow_domains
        : FREE_LIFE_ALLOW_DOMAINS,
    intervals: b.intervals || null,
    content_score: b.content_score || null,
  };
}

/**
 * Classify host: allow wins over porn/watch/social.
 * @returns {"allow"|"porn"|"watch"|"social"|"other"}
 */
function classifyHostCategory(host, policy) {
  var h = String(host || "")
    .toLowerCase()
    .replace(/^www\./, "");
  if (!h) return "other";
  if (
    h === "localhost" ||
    h === "127.0.0.1" ||
    listMatch(h, policy.allow_domains) ||
    isScalerAttachmentHost(h)
  ) {
    return "allow";
  }
  // Free / errands-lite: shopping & house sites allowed (study blocks unless allow_free_life)
  var freeLife =
    policy.mode === "free" ||
    policy.allow_free_life === true ||
    (policy.mode || "") === "errands";
  var lifeList =
    policy.free_life_allow_domains && policy.free_life_allow_domains.length
      ? policy.free_life_allow_domains
      : FREE_LIFE_ALLOW_DOMAINS || [];
  if (freeLife && listMatch(h, lifeList)) {
    return "allow";
  }
  if (listMatch(h, policy.porn_domains)) return "porn";
  var suffixes = policy.porn_suffixes || [];
  for (var i = 0; i < suffixes.length; i++) {
    if (h.endsWith(suffixes[i])) return "porn";
  }
  if (listMatch(h, policy.watch_domains)) return "watch";
  if (listMatch(h, policy.social_domains)) return "social";
  return "other";
}

/** Legacy helper — watch/social seed only (no allow/porn). Prefer shouldBlockUrl. */
function isDistractionUrl(url) {
  if (!url || typeof url !== "string") return false;
  var host = hostnameFromUrl(url);
  if (!host) return false;
  var policy = browserPolicyOrFallback(null);
  var cat = classifyHostCategory(host, policy);
  return cat === "watch" || cat === "social";
}

/**
 * Whether extension should redirect this tab URL given cached gate.
 * Allowlist never redirects (strict modes also require localhost SPA paths).
 * Porn/watch/social/keywords/other respect server flags + browser.mode.
 * Keywords: URL path/query + optional page title only (no DOM scraping).
 * @returns {boolean}
 */
/** Incubation break — never treat as free (beats day_unlimited / reward). */
function isIncubationActive(gateCache) {
  if (!gateCache) return false;
  var inc = gateCache.incubation;
  if (inc && inc.active) return true;
  var browser = browserPolicyOrFallback(gateCache.browser);
  return Boolean(browser.incubation_active);
}

/** FREE / goal met / earned reward day — YouTube+games open; porn/keywords still blocked. */
function isFreeDay(gateCache) {
  if (!gateCache) return false;
  if (isIncubationActive(gateCache)) return false;
  var browser = browserPolicyOrFallback(gateCache.browser);
  return (
    browser.mode === "free" ||
    Boolean(gateCache.day_unlimited) ||
    Boolean(gateCache.reward_day) ||
    Boolean(gateCache.unlocked && !isStrictDayMode(browser.mode))
  );
}

function shouldBlockUrl(url, gateCache, title) {
  if (!url || typeof url !== "string") return false;
  // ok=true for live gate; degraded/stale still enforces last known strict mode (fail-closed).
  if (!gateCache || (!gateCache.ok && !gateCache.degraded)) return false;
  var browser = browserPolicyOrFallback(gateCache.browser);
  var host = hostnameFromUrl(url);
  if (!host) return false;

  var cat = classifyHostCategory(host, browser);
  var freeDay = isFreeDay(gateCache);

  // Distraction filter always on (before enforce) — uses live porn lists when present.
  if (isForcePornHost(host, browser) || cat === "porn") return true;
  // Keywords on non-allow hosts always (allowlist still wins — e.g. Colab notebook ids).
  if (cat !== "allow" && browser.block_keywords !== false) {
    var adultHit = textMatchesKeywords(
      url + " " + (title || ""),
      browser.block_keywords_list
    );
    if (adultHit) return true;
  }

  // FREE / goal met / reward day: never softLand YouTube/social/other — distractions above.
  if (freeDay) return false;

  var enforce =
    Boolean(browser.enforce) ||
    Boolean(gateCache.enforce) ||
    Boolean(gateCache.locked) ||
    Boolean(isIncubationActive(gateCache)) ||
    Boolean(gateCache.degraded && isStrictDayMode(browser.mode));
  if (!enforce) return false;

  // Incubation: no temp-allow bypass for leisure.
  if (isIncubationActive(gateCache) && cat !== "allow") {
    if (cat === "watch" || cat === "social" || cat === "porn") return true;
  }

  // Strict day: all watch hosts from server list (not a baked subset).
  if (isForceWatchHost(host, browser) && isStrictDayMode(browser.mode)) return true;

  // Per-host temp allow (locked.html "Allow this site 60 sec") — never watch/porn/social.
  var tempAllows = gateCache.temp_allows || gateCache.tempAllows || [];
  if (isHostTempAllowed(host, tempAllows, Date.now(), browser)) {
    return false;
  }

  if (cat === "allow") {
    if (browser.strict_allowlist && (host === "localhost" || host === "127.0.0.1")) {
      if (!isCaltSpaUrl(url)) return true;
    }
    return false;
  }
  if (cat === "watch" && browser.block_watch_sites) return true;
  if (cat === "social" && browser.block_social) return true;
  if (browser.block_other) return true;
  return false;
}

/** SoftLand reason strings — keep aligned with native softland_decide.cpp. */
function blockKindForUrl(url, gateCache, title) {
  var browser = browserPolicyOrFallback(gateCache && gateCache.browser);
  var morning = (gateCache && gateCache.morning) || {};
  var next = browser.morning_next || morning.next || "open";
  var mode = String(browser.mode || "free").toLowerCase();
  if (next === "bible" || mode === "bible") return "morning_bible";
  if (next === "plan" || mode === "planning") return "morning_plan";
  if (gateCache && gateCache.incubation && gateCache.incubation.active) return "incubation";
  if (browser.incubation_active) return "incubation";
  var host = hostnameFromUrl(url);
  var cat = classifyHostCategory(host, browser);
  if (cat === "porn") return "porn";
  if (browser.block_keywords) {
    var hit = textMatchesKeywords(url + " " + (title || ""), browser.block_keywords_list);
    if (hit) return "keyword";
  }
  if (cat === "watch" || cat === "social") return "watch_list";
  if (mode === "study") return "study_block";
  return "softland_block";
}

/**
 * Soft-landing: morning bible/plan SPA, else extension locked.html.
 */
function redirectTargetUrl(gateCache, lockedPageUrl) {
  var browser = browserPolicyOrFallback(gateCache && gateCache.browser);
  var morning = (gateCache && gateCache.morning) || {};
  var next = browser.morning_next || morning.next || "open";
  var mode = browser.mode || "free";
  if (next === "bible" || mode === "bible") {
    return browser.bible_url || morning.bible_url || CALT_BIBLE_URL;
  }
  if (next === "plan" || mode === "planning") {
    return browser.plan_url || morning.plan_url || CALT_PRODUCTIVITY_URL;
  }
  if (browser.redirect_url) return browser.redirect_url;
  return lockedPageUrl;
}

function isExtensionOrInternalUrl(url) {
  if (!url) return true;
  return (
    url.startsWith("chrome://") ||
    url.startsWith("chrome-extension://") ||
    url.startsWith("edge://") ||
    url.startsWith("about:") ||
    url.startsWith("moz-extension://") ||
    url.startsWith("devtools://")
  );
}

function isCaltSpaUrl(url) {
  // Any local CALT origin (Vite SPA / API) — SelfTracker must not SESSION_END these;
  // study minutes come from POST /api/behavior/study-presence (active productive lanes only).
  if (!url) return false;
  try {
    var u = new URL(url);
    var host = u.hostname.toLowerCase();
    return host === "localhost" || host === "127.0.0.1";
  } catch (e) {
    return false;
  }
}
