// AUTO-BUILT by scripts/build.mjs — Gate v1.1.23

// ----- begin gate_policy.js -----
/**
 * Shared distraction-gate helpers for Chromium + Firefox SelfTracker.
 * Edge-only SelfTracker gate policy (Chromium MV3).
 * Server `browser` payload is source of truth; seeds below are offline fallback.
 *
 * Keyword = URL + title; content_score.js adds weighted DOM text/link scoring.
 * Gate refresh ~4s when browsing; 1 min idle alarm backup.
 */
/* eslint-disable no-unused-vars */

var GATE_API_URL = "http://127.0.0.1:8000/api/behavior/distraction-gate";
var GATE_ALERT_URL = "http://127.0.0.1:8000/api/behavior/gate-alert";
var GATE_EXT_LOG_URL = "http://127.0.0.1:8000/api/behavior/gate-extension-log";
var CALT_TAB_CMD_URL = "http://127.0.0.1:8000/api/behavior/calt-tab-command";
var GATE_NOTIFY_WS_URL = "ws://127.0.0.1:8000/ws/gate";
var CALT_BIBLE_URL = "http://127.0.0.1:5180/bible";
var CALT_PRODUCTIVITY_URL = "http://127.0.0.1:5180/productivity?tab=plan";
var CALT_ORIGIN = "http://127.0.0.1:5180";
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
  // Travel / IRCTC / flights (also on FALLBACK_ALLOW for study)
  "irctc.co.in",
  "confirtkt.com",
  "makemytrip.com",
  "goibibo.com",
  "cleartrip.com",
  "ixigo.com",
  "yatra.com",
  "easeemytrip.com",
  "spicejet.com",
  "goindigo.in",
  "airindia.com",
  "airindiaexpress.com",
  "akasaair.com",
  "kayak.com",
  "expedia.com",
  "expedia.co.in",
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
  "gstatic.com",
  "apis.google.com",
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
  // Travel / IRCTC / flights — study-allowed
  "irctc.co.in",
  "confirtkt.com",
  "makemytrip.com",
  "goibibo.com",
  "cleartrip.com",
  "ixigo.com",
  "yatra.com",
  "easeemytrip.com",
  "spicejet.com",
  "goindigo.in",
  "airindia.com",
  "airindiaexpress.com",
  "akasaair.com",
  "kayak.com",
  "expedia.com",
  "expedia.co.in",
  // Data science / math / coding knowledge
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
  "math.stackexchange.com",
  "wolframalpha.com",
  "mathworld.wolfram.com",
  "symbolab.com",
  "desmos.com",
  "geeksforgeeks.org",
  "cppreference.com",
  "3blue1brown.com",
  "projecteuler.net",
  "artofproblemsolving.com",
  "oeis.org",
  "mathisfun.com",
  "tutorial.math.lamar.edu",
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
  "hentaicity.com",
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
  "sxyprn.com",
  "sxyprn.net",
  "sxyporn.com",
  "thothub.to",
  "thothub.lol",
  "thothub.org",
  "hanime.tv",
  "hitomi.la",
  "gelbooru.com",
  "e621.net",
  "realbooru.com",
  "hypnotube.com",
  "hclips.com",
  "daftsex.com",
  "xlecx.one",
  "perchance.org",
  "perchance.ai",
];
var FORCE_SENSITIVE_HOSTS = [
  "grok.com",
  "grok.x.ai",
  "x.ai",
  "accounts.x.ai",
  "reddit.com",
  "redd.it",
  "venice.ai",
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
/** Quinn (Qwen) appeal grant — longer than one-click temp allow. */
var APPEAL_ALLOW_MS = 10 * 60 * 1000;
var APPEAL_ALLOW_STORAGE_KEY = "appealAllows";
/** Default local Qwen via Ollama — matches Focus `calt:focus-llm-host:v1`. */
var QWEN_DEFAULT_BASE = "http://127.0.0.1:11434/v1";
var QWEN_DEFAULT_MODEL = "qwen2.5:3b";

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

// BEGIN GENERATED_CONTENT_SCORE (scripts/sync_gate_policy_seeds.py)
/** Weighted page-text score — generated from backend.behavior.content_score. */
var CONTENT_SCORE_WEIGHTS = {
  pornography: 4,
  "hardcore porn": 5,
  porn: 4,
  porno: 4,
  "xxx video": 5,
  xxx: 3,
  nsfw: 2,
  hentai: 4,
  onlyfans: 4,
  fansly: 3,
  chaturbate: 4,
  blowjob: 5,
  handjob: 5,
  cumshot: 5,
  creampie: 5,
  gangbang: 5,
  threesome: 4,
  deepthroat: 5,
  bdsm: 3,
  bondage: 3,
  fetish: 2,
  pegging: 4,
  milf: 3,
  incest: 5,
  rule34: 4,
  "rule 34": 4,
  "nude pics": 4,
  "nude photo": 4,
  nudes: 3,
  nudity: 2,
  nude: 2,
  "naked pics": 4,
  "naked photo": 4,
  erotic: 2,
  erotica: 2,
  "live sex": 5,
  "sex cam": 5,
  "sex tape": 5,
  "sex video": 4,
  "adult video": 3,
  camgirl: 4,
  "cam girl": 4,
  erome: 4,
  eporner: 4,
  redgifs: 4,
  pornhub: 4,
  xvideos: 4,
  xhamster: 4,
};

var CONTENT_SCORE_THRESHOLDS = {
  // Keyword-only adult lock (NSFW image scanner paused). Higher floors cut false porn labels.
  free: { warn: 18, lock: 28, maxSamples: 5 },
  study: { warn: 14, lock: 24, maxSamples: 4 },
};
// END GENERATED_CONTENT_SCORE
/** Apply live content_score from browser payload (Python content_score.py). */
function applyContentScoreFromPolicy(policy) {
  var cs = policy && policy.content_score;
  if (!cs || typeof cs !== "object") return;
  if (cs.weights && typeof cs.weights === "object") {
    CONTENT_SCORE_WEIGHTS = cs.weights;
  }
  if (cs.thresholds && typeof cs.thresholds === "object") {
    var free = cs.thresholds.free || {};
    var study = cs.thresholds.study || {};
    CONTENT_SCORE_THRESHOLDS = {
      free: {
        warn: free.warn != null ? free.warn : 18,
        lock: free.lock != null ? free.lock : 28,
        maxSamples: free.max_samples != null ? free.max_samples : free.maxSamples != null ? free.maxSamples : 5,
      },
      study: {
        warn: study.warn != null ? study.warn : 14,
        lock: study.lock != null ? study.lock : 24,
        maxSamples:
          study.max_samples != null ? study.max_samples : study.maxSamples != null ? study.maxSamples : 4,
      },
    };
  }
}

function scorePageHaystack(text, weights, perTermCap) {
  var original = String(text || "");
  var hay = original.toLowerCase();
  var wmap = weights || CONTENT_SCORE_WEIGHTS;
  var cap = typeof perTermCap === "number" ? perTermCap : 3;
  var terms = Object.keys(wmap).sort(function (a, b) {
    return b.length - a.length;
  });
  var score = 0;
  var matched = [];
  var details = [];
  for (var i = 0; i < terms.length; i++) {
    var term = terms[i];
    var weight = Number(wmap[term]) || 0;
    if (weight <= 0 || term.length < 3) continue;
    var escaped = term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    var re = new RegExp("(^|[^a-z0-9])" + escaped + "([^a-z0-9]|$)", "gi");
    var hits = 0;
    var m;
    while ((m = re.exec(hay)) !== null) {
      hits++;
      if (m.index === re.lastIndex) re.lastIndex++;
    }
    if (hits <= 0) continue;
    var use = Math.min(hits, cap);
    var contrib = weight * use;
    score += contrib;
    matched.push(term);
    details.push({
      term: term,
      weight: weight,
      hits: hits,
      used: use,
      contrib: contrib,
    });
    hay = hay.replace(new RegExp("(^|[^a-z0-9])" + escaped + "([^a-z0-9]|$)", "gi"), "$1 $2");
  }
  details.sort(function (a, b) {
    return b.contrib - a.contrib;
  });
  for (var d = 0; d < details.length; d++) {
    details[d].pct = score > 0 ? Math.round((details[d].contrib / score) * 1000) / 10 : 0;
  }
  return { score: score, matched: matched, details: details, textLen: original.length };
}

function decideContentScoreAction(opts) {
  opts = opts || {};
  var mode = String(opts.mode || "free").toLowerCase() === "study" ? "study" : "free";
  var th = CONTENT_SCORE_THRESHOLDS[mode] || { warn: 8, lock: 10, maxSamples: 5 };
  var score = Number(opts.score) || 0;
  var prev = Number(opts.prevScore) || 0;
  var idx = Number(opts.sampleIndex) || 0;
  var warned = Boolean(opts.warned);
  // Over lock → block immediately (SoftLand allow-list cannot save this).
  if (score >= th.lock) return "lock";
  if (score >= th.warn) {
    // Stable warn-band scores must lock on the next sample — do NOT require score > prev.
    // (Old score>prev rule let flat adult pages warn once then stop without locking.)
    if (warned || idx >= 1) return "lock";
    return "warn";
  }
  if (idx >= 1 && score < th.warn && Math.abs(score - prev) <= 1) return "stop";
  if (idx + 1 >= th.maxSamples) return "stop";
  return "continue";
}

/**
 * Sentence windows around keyword hits — keyword score alone false-positives
 * Gemini/ChatGPT "how to block adult content" guides. Returns allow:true|false|null.
 */
var SENTENCE_ADVICE_RES = [
  /\b(block|blocking|blocked|blocker)\b.{0,40}\b(porn|pornography|adult|nsfw|xxx)\b/i,
  /\b(porn|pornography|adult|nsfw|xxx)\b.{0,40}\b(block|blocking|filter|filters|filtering|restrict|restriction)\b/i,
  /\b(content\s+filter|parental\s+control|family\s+safety|safe\s*search|web\s+filtering)\b/i,
  /\b(cleanbrowsing|opendns|cloudflare\s+family|dns).{0,30}\b(adult|family|filter)\b/i,
  /\b(filter|limit)\s+(inappropriate|adult)\s+(websites?|content|searches?)\b/i,
  /\b(how\s+to\s+block|ways\s+to\s+(set\s+up|block)|network[- ]level\s+blocking)\b/i,
  /\b(never\s+allow|screen\s+time|content\s+&\s+privacy|content\s+restrictions)\b/i,
];
var SENTENCE_EROTIC_RES = [
  /\b(watch|stream|streaming|free)\b.{0,24}\b(porn|xxx|hentai|sex\s+video|adult\s+video)\b/i,
  /\b(hot\s+girls?|cam\s*girls?|live\s+sex|sex\s+cam|nude\s+pics?|naked\s+pics?)\b/i,
  /\b(pornhub|xvideos|xhamster|chaturbate|onlyfans|redgifs|eporner)\b/i,
  /\b(blowjob|handjob|cumshot|creampie|gangbang|deepthroat|rule\s*34)\b/i,
];

function extractKeywordSentenceWindows(haystack, matched, radius) {
  var hay = String(haystack || "");
  var low = hay.toLowerCase();
  var r = typeof radius === "number" ? radius : 140;
  var terms = Array.isArray(matched) ? matched : [];
  var out = [];
  var seen = Object.create(null);
  for (var i = 0; i < terms.length && out.length < 8; i++) {
    var term = String(terms[i] || "").toLowerCase();
    if (!term || term.length < 3) continue;
    var idx = 0;
    var hits = 0;
    while (hits < 2) {
      var at = low.indexOf(term, idx);
      if (at < 0) break;
      var start = Math.max(0, at - r);
      var end = Math.min(hay.length, at + term.length + r);
      var win = hay.slice(start, end).replace(/\s+/g, " ").trim();
      var key = win.slice(0, 80);
      if (win && !seen[key]) {
        seen[key] = true;
        out.push(win);
      }
      idx = at + term.length;
      hits++;
    }
  }
  if (!out.length && hay) out.push(hay.slice(0, Math.min(hay.length, 500)));
  return out;
}

/**
 * Local sentence confirmation after keyword score.
 * allow:true = clearly "how to block / filter adult content" discussion
 * allow:false = erotic / tube language near hits
 * allow:null = inconclusive (Quinn may review; else fail-closed lock)
 */
function sentenceConfirmKeywordHit(haystack, matched) {
  var windows = extractKeywordSentenceWindows(haystack, matched, 160);
  var advice = 0;
  var erotic = 0;
  var sample = "";
  for (var i = 0; i < windows.length; i++) {
    var w = windows[i];
    if (!sample) sample = w.slice(0, 180);
    var a = false;
    var e = false;
    for (var j = 0; j < SENTENCE_ADVICE_RES.length; j++) {
      if (SENTENCE_ADVICE_RES[j].test(w)) {
        a = true;
        break;
      }
    }
    for (var k = 0; k < SENTENCE_EROTIC_RES.length; k++) {
      if (SENTENCE_EROTIC_RES[k].test(w)) {
        e = true;
        break;
      }
    }
    if (a) advice++;
    if (e) erotic++;
  }
  if (erotic > 0 && erotic >= advice) {
    return {
      allow: false,
      source: "sentence_local",
      why: "Erotic language near keyword hits.",
      advice: advice,
      erotic: erotic,
      sample: sample,
    };
  }
  if (advice >= 2 && erotic === 0) {
    return {
      allow: true,
      source: "sentence_local",
      why: "Looks like blocking / filter advice, not adult media.",
      advice: advice,
      erotic: erotic,
      sample: sample,
    };
  }
  if (advice >= 1 && erotic === 0) {
    var softOnly = true;
    var soft = { porn: 1, porno: 1, pornography: 1, nsfw: 1, adult: 1, nude: 1, nudity: 1, erotic: 1 };
    for (var t = 0; t < (matched || []).length; t++) {
      var m = String(matched[t] || "").toLowerCase();
      if (!soft[m] && m.indexOf("adult") < 0) softOnly = false;
    }
    if (softOnly) {
      return {
        allow: true,
        source: "sentence_local",
        why: "Soft keyword + blocking-advice sentence context.",
        advice: advice,
        erotic: erotic,
        sample: sample,
      };
    }
  }
  return {
    allow: null,
    source: "sentence_local",
    why: "Inconclusive sentence context.",
    advice: advice,
    erotic: erotic,
    sample: sample,
  };
}

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

/**
 * SoftLand `allow_domains` / `watch_domains` are extras (or a partial list).
 * Always union with offline FALLBACK so a tiny allow_extra (e.g. example.com)
 * cannot wipe google.com / github / numpy and lock study browsing.
 */
function mergeHostLists(base, extra) {
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
    }
  }
  pushAll(base);
  pushAll(extra);
  return out;
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
  // Always union offline seeds so a stale SoftLand payload cannot drop new tubes
  // (e.g. hentaicity.com) from DNR.
  var live = null;
  if (policy && policy.force_porn_hosts && policy.force_porn_hosts.length) {
    live = policy.force_porn_hosts;
  } else if (policy && policy.porn_domains && policy.porn_domains.length) {
    live = policy.porn_domains;
  }
  var out = [];
  var seen = Object.create(null);
  function add(list) {
    if (!list || !list.length) return;
    for (var i = 0; i < list.length; i++) {
      var h = String(list[i] || "")
        .toLowerCase()
        .replace(/^www\./, "")
        .trim();
      if (!h || seen[h]) continue;
      seen[h] = true;
      out.push(h);
    }
  }
  add(live);
  add(FORCE_PORN_HOSTS);
  return out;
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
  // Mirror msg_host LooksPorn tokens so DNR / Inspect paths catch mirrors.
  var tokens = [
    "porn",
    "xvideos",
    "xnxx",
    "xhamster",
    "youporn",
    "redtube",
    "hentai",
    "onlyfans",
    "chaturbate",
    "spankbang",
    "nhentai",
    "rule34",
    "fansly",
    "stripchat",
    "sxyprn",
    "sxyporn",
    "thothub",
    "hanime",
    "gelbooru",
    "danbooru",
    "hitomi",
    "e621",
    "fapello",
    "missav",
    "daftsex",
    "hypnotube",
    "hclips",
    "perchance",
  ];
  for (var t = 0; t < tokens.length; t++) {
    if (h.indexOf(tokens[t]) >= 0) return true;
  }
  return false;
}

function isSensitiveHost(host, policy) {
  var h = normalizeHost(host);
  if (!h) return false;
  var pol = policy || {};
  var list =
    pol.sensitive_domains && pol.sensitive_domains.length
      ? pol.sensitive_domains
      : typeof FORCE_SENSITIVE_HOSTS !== "undefined"
        ? FORCE_SENSITIVE_HOSTS
        : [];
  return listMatch(h, list);
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
  if (isForceWatchHost(h, pol) || isForcePornHost(h, pol) || isSensitiveHost(h, pol)) return true;
  var cat = classifyHostCategory(h, pol);
  return cat === "watch" || cat === "porn" || cat === "social";
}

/**
 * Hard deny for Quinn appeals — known adult tubes / suffixes never get a pass,
 * even if the model hallucinates "genuine".
 */
function isAppealHardDenyHost(host, policy) {
  var h = normalizeHost(host);
  if (!h) return true;
  var pol = policy || browserPolicyOrFallback(null);
  if (isForcePornHost(h, pol)) return true;
  var suffixes = forcePornSuffixes(pol);
  for (var i = 0; i < suffixes.length; i++) {
    if (h.endsWith(suffixes[i])) return true;
  }
  // Same token set as isForcePornHost / LooksPorn (obfuscated tubes included).
  if (isForcePornHost(h, pol)) return true;
  return false;
}

function pruneAppealAllows(list, now) {
  var t = typeof now === "number" ? now : Date.now();
  var out = [];
  if (!list || !list.length) return out;
  for (var i = 0; i < list.length; i++) {
    var e = list[i];
    if (!e || !e.host) continue;
    var until = Number(e.until);
    if (!until || until <= t) continue;
    out.push({
      host: normalizeHost(e.host),
      until: until,
      why: e.why || "",
      by: e.by || "qwen",
    });
  }
  return out;
}

function isHostAppealAllowed(host, appealAllows, now) {
  var h = normalizeHost(host);
  if (!h) return false;
  var t = typeof now === "number" ? now : Date.now();
  var list = pruneAppealAllows(appealAllows, t);
  for (var i = 0; i < list.length; i++) {
    if (hostMatchesDomain(h, list[i].host) || hostMatchesDomain(list[i].host, h)) {
      if (t < list[i].until) return true;
    }
  }
  return false;
}

function appealAllowUntilForHost(host, appealAllows, now) {
  var h = normalizeHost(host);
  if (!h) return 0;
  var t = typeof now === "number" ? now : Date.now();
  var list = pruneAppealAllows(appealAllows, t);
  var best = 0;
  for (var i = 0; i < list.length; i++) {
    if (hostMatchesDomain(h, list[i].host) || hostMatchesDomain(list[i].host, h)) {
      if (list[i].until > best) best = list[i].until;
    }
  }
  return best > t ? best : 0;
}

function upsertAppealAllow(list, entry, now) {
  var t = typeof now === "number" ? now : Date.now();
  var out = pruneAppealAllows(list, t);
  var h = normalizeHost(entry && entry.host);
  if (!h) return out;
  var next = [];
  for (var i = 0; i < out.length; i++) {
    if (out[i].host !== h) next.push(out[i]);
  }
  next.push({
    host: h,
    until: Number(entry.until) || t + APPEAL_ALLOW_MS,
    why: (entry && entry.why) || "",
    by: (entry && entry.by) || "qwen",
  });
  return next;
}

/**
 * Bing / Google / DDG results pages carry tracking params (gs_lcrp, cvid, form)
 * that are not the search the user typed. Judge those hosts by q/query/p/text.
 * Returns null when the URL is not a search-results page.
 */
function webSearchQuery(url) {
  var u;
  try {
    u = new URL(String(url || ""));
  } catch (e) {
    return null;
  }
  var h = u.hostname.toLowerCase().replace(/^www\./, "");
  var searchHost =
    /(^|\.)google\./.test(h) ||
    h === "bing.com" ||
    h.endsWith(".bing.com") ||
    h === "duckduckgo.com" ||
    h.endsWith(".duckduckgo.com") ||
    h === "search.brave.com" ||
    h === "ecosia.org" ||
    h === "startpage.com" ||
    h === "kagi.com" ||
    h === "search.yahoo.com" ||
    h === "yahoo.com";
  if (!searchHost) return null;
  var q =
    u.searchParams.get("q") ||
    u.searchParams.get("query") ||
    u.searchParams.get("p") ||
    u.searchParams.get("text") ||
    "";
  var searchPath = u.pathname.indexOf("/search") >= 0 || h.indexOf("duckduckgo.") >= 0;
  if (!searchPath && !q) return null;
  return q;
}

/** Full URL for ordinary hosts. Search engines: the typed query and title only. */
function adultMatchHay(url, title) {
  var query = webSearchQuery(url);
  if (query !== null) return (query + " " + String(title || "")).toLowerCase();
  return (String(url || "") + " " + String(title || "")).toLowerCase();
}

/**
 * URL/path cheat catch — hosts that hide "porn" in the domain still often put
 * adult words in the path, query, or title. Used when the seed list misses a mirror.
 */
function urlLooksAdult(url, title) {
  var hay = adultMatchHay(url, title);
  if (!hay.trim()) return false;
  var tokens = [
    "porn",
    "xxx",
    "hentai",
    "onlyfans",
    "nsfw",
    "blowjob",
    "cumshot",
    "creampie",
    "gangbang",
    "deepthroat",
    "xvideos",
    "pornhub",
    "xnxx",
    "xhamster",
    "rule34",
    "fapello",
    "chaturbate",
    "stripchat",
    "sxyprn",
    "thothub",
    "hanime",
    "gelbooru",
    "e621",
    "/sex/",
    "sex-",
    "-sex",
    "hardcore",
    "nude",
    "milf",
    "incest",
  ];
  for (var i = 0; i < tokens.length; i++) {
    if (hay.indexOf(tokens[i]) >= 0) return true;
  }
  return false;
}

/** Tube names and sex acts — not the bare words porn/nsfw/xxx on a code or docs page. */
var URL_EXPLICIT_TOKENS = [
  "hentai",
  "onlyfans",
  "blowjob",
  "cumshot",
  "creampie",
  "gangbang",
  "deepthroat",
  "xvideos",
  "pornhub",
  "xnxx",
  "xhamster",
  "rule34",
  "fapello",
  "chaturbate",
  "stripchat",
  "sxyprn",
  "thothub",
  "hanime",
  "gelbooru",
  "e621",
  "milf",
  "incest",
];

function urlLooksExplicit(url, title) {
  var hay = adultMatchHay(url, title);
  if (!hay.trim()) return false;
  for (var i = 0; i < URL_EXPLICIT_TOKENS.length; i++) {
    if (hay.indexOf(URL_EXPLICIT_TOKENS[i]) >= 0) return true;
  }
  return false;
}

function isFallbackAllowHost(host) {
  return listMatch(normalizeHost(host), FALLBACK_ALLOW_DOMAINS);
}

/** User note on the locked page: research / code / working on the blocker itself. */
function noteLooksResearch(note) {
  return /\b(rese?a?rch|reseach|reasearch|reserch|giu?thub|githu?b|gitlab|stackoverflow|wiki(pedia)?|academic|docs|documentation|repo|classifier|blocking|block\s*list|false\s*positive|paper|porn\s*block|block(ing)?\s+porn|helping\s+me|claude|chatgpt|gemini)\b/i.test(
    String(note || "")
  );
}

function noteLooksWatchRequest(note) {
  return /\b(let me watch|just (this|one) (video|porn)|open (the )?(porn|tube)|wanna watch)\b/i.test(
    String(note || "")
  );
}

/**
 * Allow-listed hosts (github, docs, wikipedia) are not tubes.
 * Lock them only for an explicit URL or erotic sentence context, not the word "porn" in a repo.
 */
function allowHostContentLock(opts) {
  opts = opts || {};
  var host = opts.host || "";
  if (!isFallbackAllowHost(host)) return { skip: false };
  if (typeof isForcePornHost === "function" && isForcePornHost(host, null)) return { skip: false };
  if (typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, null)) return { skip: false };
  var explicit =
    typeof urlLooksExplicit === "function" && urlLooksExplicit(opts.url || "", opts.title || "");
  if (explicit) return { skip: false, why: "explicit_url" };
  var sentence =
    typeof sentenceConfirmKeywordHit === "function"
      ? sentenceConfirmKeywordHit(
          String(opts.title || "") + "\n" + String(opts.excerpt || opts.hay || ""),
          opts.matched || []
        )
      : { allow: null };
  if (sentence.allow === false) return { skip: false, why: "sentence_erotic" };
  // Allow-listed hosts (Claude, GitHub, docs): never lock on keyword score alone.
  // Chats about building a porn blocker are full of the word "porn".
  return { skip: true, why: sentence.allow === true ? "sentence_advice" : "allow_host_soft" };
}

/** Reasons on locked.html that may show Appeal to Quinn. */
function isPornAppealReason(reason) {
  var r = String(reason || "")
    .toLowerCase()
    .replace(/-/g, "_");
  // Study / SoftLand watch locks are NOT porn — do not offer Quinn adult appeal UI.
  if (
    r === "study" ||
    r === "watch_study" ||
    r === "watch" ||
    r === "blocked" ||
    r === "sensitive" ||
    r === "morning" ||
    r === "bible" ||
    r === "plan" ||
    r === "incubation" ||
    r === "keyword"
  ) {
    return false;
  }
  return r === "porn" || r === "content_score" || r.indexOf("porn") >= 0;
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
        ? ["/bible", "/productivity", "/profile", "/journal"]
        : [];
  return {
    mode: mode,
    mode_label: b.mode_label || mode.toUpperCase(),
    // SoftLand publishes site_rules.allow_extra as allow_domains — merge, never replace.
    allow_domains: mergeHostLists(FALLBACK_ALLOW_DOMAINS, b.allow_domains),
    watch_domains: mergeHostLists(FALLBACK_WATCH_DOMAINS, b.watch_domains),
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
    block_sensitive: b.block_sensitive !== false,
    sensitive_domains:
      b.sensitive_domains && b.sensitive_domains.length
        ? b.sensitive_domains
        : typeof FORCE_SENSITIVE_HOSTS !== "undefined"
          ? FORCE_SENSITIVE_HOSTS
          : [],
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
  // Grok, Reddit, Venice: direct block. Allow-list does not open them.
  if (browser.block_sensitive !== false && isSensitiveHost(host, browser)) return true;
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
  // Native uses short reason tokens on locked.html ?why=
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
  // Any local CALT origin — gate soft-land skips; SelfTracker owns internet tabs only.
  if (!url) return false;
  try {
    var u = new URL(url);
    var host = u.hostname.toLowerCase();
    return host === "localhost" || host === "127.0.0.1";
  } catch (e) {
    return false;
  }
}

// ----- end gate_policy.js -----

// ----- begin v2_bundle.js -----
(() => {
  // src/gate_config.js
  var GATE_V2_DEFAULTS = {
    enabled: true,
    shadowOnly: false,
    version: "2.0.1",
    score: {
      /** under this → ALLOW */
      allowBelow: 50,
      /** allowBelow..watchBelow-1 → WATCH; watchBelow+ or hard → LOCK */
      watchBelow: 80,
      /** persist every verdict with score ≥ this (normal-usage telemetry) */
      highScoreLogAt: 50
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
      metaAdult: 45
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
      cacheMax: 400
    },
    appeal: {
      cooldownMs: 15 * 60 * 1e3,
      grantMs: 30 * 60 * 1e3,
      maxGrantsPerDay: 6
    },
    blocklist: {
      alarmMinutes: 60 * 24 * 7,
      storageKey: "gateBlocklistV2",
      maxHosts: 35e4
    },
    log: {
      maxEntries: 500,
      storageKey: "gateVerdictLogV2",
      highScoreKey: "gateHighScoreLogV2",
      highScoreMax: 300,
      thresholdsKey: "gateThresholdsV2"
    },
    looseningDelayMs: 24 * 60 * 60 * 1e3
  };
  var GATE_V2 = JSON.parse(JSON.stringify(GATE_V2_DEFAULTS));
  function snapshotThresholds() {
    return {
      allowBelow: GATE_V2.score.allowBelow,
      watchBelow: GATE_V2.score.watchBelow,
      highScoreLogAt: GATE_V2.score.highScoreLogAt,
      imagePornHentaiLock: GATE_V2.image.pornHentaiLock,
      aggregateMin: GATE_V2.image.aggregateMin,
      aggregateCount: GATE_V2.image.aggregateCount,
      sexyOnly: GATE_V2.image.sexyOnly,
      savedAt: Date.now()
    };
  }
  function applyThresholdOverrides(partial) {
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
  async function loadSavedThresholds(storage) {
    if (!storage || !storage.local) return snapshotThresholds();
    try {
      const key = GATE_V2.log.thresholdsKey;
      const r = await storage.local.get(key);
      if (r && r[key]) applyThresholdOverrides(r[key]);
    } catch (e) {
    }
    return snapshotThresholds();
  }
  async function saveThresholds(storage, partial) {
    const snap = applyThresholdOverrides(partial || snapshotThresholds());
    if (storage && storage.local) {
      try {
        await storage.local.set({ [GATE_V2.log.thresholdsKey]: snap });
      } catch (e) {
      }
    }
    return snap;
  }
  var RECOVERY_SEARCH_RE = /\bbreast\s+cancer\b|\bsex\s+education\b|\b(quit|quitting|stop|stopping|block|blocking|addiction|filter|filters|recovery|recover|help|helpline|education|educational|cancer|health|therapy|religious|bible)\b.{0,40}\b(porn|pornography|sex|nsfw|xxx|hentai)\b|\b(porn|pornography|sex)\b.{0,40}\b(addiction|block|blocking|quit|education|cancer|health|filter)\b/i;

  // data/host_seed.json
  var host_seed_default = {
    version: 1,
    suffixes: [".xxx", ".adult", ".porn", ".sex"],
    host_labels: ["porn", "xvideos", "xnxx", "xhamster", "youporn", "redtube", "hentai", "onlyfans", "chaturbate", "spankbang", "eporner", "porntrex", "rule34", "nhentai", "fansly", "sxyprn", "sxyporn", "thothub", "hanime", "gelbooru", "danbooru", "hitomi", "e621", "fapello", "missav", "daftsex", "hypnotube", "hclips", "perchance"],
    tiers: {
      PORN_STRICT: [
        "pornhub.com",
        "pornhub.org",
        "pornhub.net",
        "xvideos.com",
        "xvideos2.com",
        "xvideos.es",
        "xnxx.com",
        "xnxx.tv",
        "xhamster.com",
        "xhamster2.com",
        "xhamster3.com",
        "redtube.com",
        "youporn.com",
        "tube8.com",
        "spankbang.com",
        "chaturbate.com",
        "onlyfans.com",
        "porn.com",
        "sex.com",
        "hentaihaven.xxx",
        "hentaicity.com",
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
        "sxyprn.com",
        "sxyprn.net",
        "sxyporn.com",
        "thothub.to",
        "thothub.lol",
        "thothub.org",
        "hanime.tv",
        "hitomi.la",
        "gelbooru.com",
        "e621.net",
        "realbooru.com",
        "hypnotube.com",
        "hclips.com",
        "daftsex.com",
        "xlecx.one",
        "perchance.org",
        "perchance.ai",
        "fansly.com",
        "manyvids.com",
        "stripchat.com",
        "bongacams.com",
        "livejasmin.com",
        "pornhd.com",
        "youjizz.com",
        "tnaflix.com",
        "drtuber.com",
        "sunporno.com",
        "nuvid.com",
        "porn300.com",
        "danbooru.donmai.us",
        "rule34.paheal.net"
      ],
      SENSITIVE: [
        "grok.com",
        "grok.x.ai",
        "x.ai",
        "accounts.x.ai",
        "reddit.com",
        "redd.it",
        "venice.ai"
      ],
      WATCH: [
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
        "zee5.com"
      ],
      SOCIAL: [
        "instagram.com",
        "x.com",
        "twitter.com",
        "tiktok.com",
        "facebook.com",
        "discord.com"
      ],
      SEARCH: [
        "google.com",
        "bing.com",
        "duckduckgo.com",
        "search.brave.com",
        "ecosia.org",
        "startpage.com",
        "kagi.com",
        "search.yahoo.com",
        "yahoo.com"
      ],
      TRUSTED: [
        "localhost",
        "127.0.0.1",
        "calt.app",
        "calt-learn.app",
        "calt-data.app",
        "calt-bible.app",
        "claude.ai",
        "anthropic.com",
        "chatgpt.com",
        "openai.com",
        "gemini.google.com",
        "aistudio.google.com",
        "accounts.google.com",
        "myaccount.google.com",
        "gstatic.com",
        "apis.google.com",
        "googleusercontent.com",
        "github.com",
        "githubusercontent.com",
        "gitlab.com",
        "stackoverflow.com",
        "stackexchange.com",
        "wikipedia.org",
        "developer.mozilla.org",
        "mdn.io",
        "colab.research.google.com",
        "scaler.com",
        "docs.google.com",
        "drive.google.com",
        "notion.so",
        "arxiv.org",
        "python.org",
        "npmjs.com",
        "cursor.com",
        "vscode.dev",
        "huggingface.co",
        "kaggle.com",
        "numpy.org",
        "pandas.pydata.org",
        "scipy.org",
        "scikit-learn.org",
        "matplotlib.org",
        "pytorch.org",
        "tensorflow.org",
        "keras.io",
        "jax.dev",
        "polars.tech",
        "pydata.org",
        "wolframalpha.com",
        "desmos.com",
        "math.stackexchange.com",
        "geeksforgeeks.org",
        "cppreference.com",
        "brilliant.org",
        "khanacademy.org",
        "leetcode.com",
        "irctc.co.in",
        "makemytrip.com",
        "goibibo.com",
        "cleartrip.com",
        "ixigo.com",
        "yatra.com",
        "spicejet.com",
        "goindigo.in",
        "airindia.com",
        "kayak.com"
      ],
      UGC: [
        "imgur.com",
        "pinterest.com",
        "tumblr.com",
        "web.telegram.org",
        "discord.com",
        "media.discordapp.net",
        "cdn.discordapp.com"
      ]
    }
  };

  // src/host_intel.js
  function normHost(h) {
    return String(h || "").toLowerCase().replace(/^www\./, "").replace(/:\d+$/, "");
  }
  function listHas(host, list) {
    const h = normHost(host);
    if (!h || !list || !list.length) return false;
    for (let i = 0; i < list.length; i++) {
      const d = normHost(list[i]);
      if (!d) continue;
      if (h === d || h.endsWith("." + d)) return true;
    }
    return false;
  }
  function parentHosts(host) {
    const h = normHost(host);
    const parts = h.split(".").filter(Boolean);
    const out = [h];
    for (let i = 1; i < parts.length - 1; i++) {
      out.push(parts.slice(i).join("."));
    }
    return out;
  }
  var blocklistSet = null;
  function setBlocklistHosts(hosts) {
    blocklistSet = /* @__PURE__ */ new Set();
    if (!hosts || !hosts.length) return;
    for (let i = 0; i < hosts.length; i++) {
      const d = normHost(hosts[i]);
      if (d && d.indexOf(".") >= 0) blocklistSet.add(d);
    }
  }
  function blocklistSize() {
    return blocklistSet ? blocklistSet.size : 0;
  }
  function inBlocklist(host) {
    if (!blocklistSet || !blocklistSet.size) return false;
    const parents = parentHosts(host);
    for (let i = 0; i < parents.length; i++) {
      if (blocklistSet.has(parents[i])) return true;
    }
    return false;
  }
  function suffixPorn(host) {
    const h = normHost(host);
    const suffixes = host_seed_default.suffixes || [];
    for (let i = 0; i < suffixes.length; i++) {
      if (h.endsWith(suffixes[i])) return true;
    }
    return false;
  }
  function labelPornHit(host) {
    const h = normHost(host);
    const labels = h.split(/[.-]/).filter(Boolean);
    const want = host_seed_default.host_labels || [];
    for (let i = 0; i < labels.length; i++) {
      for (let j = 0; j < want.length; j++) {
        if (labels[i] === want[j]) return want[j];
      }
    }
    return "";
  }
  function hostTier(host) {
    const h = normHost(host);
    if (!h) return "UNKNOWN";
    const t = host_seed_default.tiers || {};
    if (suffixPorn(h) || labelPornHit(h) || listHas(h, t.PORN_STRICT) || inBlocklist(h)) {
      return "PORN_STRICT";
    }
    if (listHas(h, t.SENSITIVE)) return "SENSITIVE";
    if (listHas(h, t.WATCH)) return "WATCH";
    if (listHas(h, t.SOCIAL)) return "SOCIAL";
    if (listHas(h, t.SEARCH)) return "SEARCH";
    if (listHas(h, t.TRUSTED)) return "TRUSTED";
    if (listHas(h, t.UGC)) return "UGC";
    return "UNKNOWN";
  }

  // src/url_tokens.js
  var ADULT_TOKENS = /* @__PURE__ */ new Set([
    "porn",
    "porno",
    "pornography",
    "xxx",
    "hentai",
    "nsfw",
    "onlyfans",
    "fansly",
    "chaturbate",
    "stripchat",
    "xvideos",
    "xnxx",
    "xhamster",
    "pornhub",
    "redtube",
    "youporn",
    "spankbang",
    "rule34",
    "nhentai",
    "eporner",
    "redgifs",
    "fapello",
    "blowjob",
    "handjob",
    "cumshot",
    "creampie",
    "gangbang",
    "deepthroat",
    "milf",
    "incest",
    "camgirl",
    "sexcam"
  ]);
  var ADULT_PHRASES = ["hardcore porn", "sex video", "adult video", "live sex", "nude pics"];
  function tokenizeText(s) {
    return String(s || "").toLowerCase().split(/[^a-z0-9]+/g).filter((t) => t.length >= 3);
  }
  function adultTokensInText(s) {
    const hay = String(s || "").toLowerCase();
    const hits = [];
    for (const p of ADULT_PHRASES) {
      if (hay.indexOf(p) >= 0) hits.push(p);
    }
    const toks = tokenizeText(hay);
    for (const t of toks) {
      if (ADULT_TOKENS.has(t)) hits.push(t);
    }
    return hits;
  }
  function isRecoverySearchQuery(q) {
    return RECOVERY_SEARCH_RE.test(String(q || ""));
  }
  function webSearchQuery(url) {
    let u;
    try {
      u = new URL(String(url || ""));
    } catch (e) {
      return null;
    }
    const h = u.hostname.toLowerCase().replace(/^www\./, "");
    const searchHost = /(^|\.)google\./.test(h) || h === "bing.com" || h.endsWith(".bing.com") || h === "duckduckgo.com" || h.endsWith(".duckduckgo.com") || h === "search.brave.com" || h === "ecosia.org" || h === "startpage.com" || h === "kagi.com" || h === "search.yahoo.com" || h === "yahoo.com";
    if (!searchHost) return null;
    const q = u.searchParams.get("q") || u.searchParams.get("query") || u.searchParams.get("p") || u.searchParams.get("text") || "";
    const searchPath = u.pathname.indexOf("/search") >= 0 || h.indexOf("duckduckgo.") >= 0;
    if (!searchPath && !q) return null;
    return q;
  }
  function urlHasAdultTokens(url, title) {
    const q = webSearchQuery(url);
    if (q !== null) return adultTokensInText(q + " " + (title || "")).length > 0;
    let path = "";
    try {
      const u = new URL(String(url || ""));
      path = u.pathname + " " + u.search;
    } catch (e) {
      path = String(url || "");
    }
    return adultTokensInText(path + " " + (title || "")).length > 0;
  }
  function isImageSearchUrl(url) {
    try {
      const u = new URL(String(url || ""));
      const h = u.hostname.toLowerCase();
      if (u.searchParams.get("tbm") === "isch") return true;
      if (h.indexOf("bing.") >= 0 && /\/images\//i.test(u.pathname)) return true;
      if (h.indexOf("duckduckgo.") >= 0 && u.searchParams.get("iax") === "images") return true;
      return false;
    } catch (e) {
      return false;
    }
  }

  // src/decision_engine.js
  function decide(input) {
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
    if (tier === "SENSITIVE" && softlandOn) {
      hard = "sensitive";
    }
    const strictMode = mode === "study" || mode === "bible" || mode === "planning";
    if (softlandOn && strictMode && (tier === "WATCH" || tier === "SOCIAL")) {
      hard = "watch_study";
    }
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
    const hasHardImage = imageOn && (ph >= cfg.image.pornHentaiLock || c035 >= cfg.image.aggregateCount);
    const quinn = input.quinn;
    if (quinn && quinn.allow && !hard && !hasHardImage && !input.rta && score >= cfg.score.allowBelow && score < cfg.score.watchBelow) {
      score = Math.max(0, score + w.quinnAllowMax);
      reasons.push("quinn_allow:" + w.quinnAllowMax);
    }
    score = Math.min(100, Math.max(0, score));
    let verdict = "ALLOW";
    if (hard) verdict = "LOCK";
    else if (score >= cfg.score.watchBelow) verdict = "LOCK";
    else if (score >= cfg.score.allowBelow) verdict = "WATCH";
    if (!hard && softlandOn && mode === "free" && (tier === "SOCIAL" || tier === "UGC" || tier === "SENSITIVE") && verdict === "ALLOW") {
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
      ts: Date.now()
    };
  }
  function mergeMonotonic(prev, next) {
    if (!prev) return next;
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
      reasons: (prev.reasons || []).concat(next.reasons || []).slice(-40)
    };
  }

  // src/unwrap.js
  function unwrapDestinationUrl(url) {
    let raw = String(url || "");
    try {
      let u = new URL(raw);
      const h = u.hostname.toLowerCase();
      if (h === "translate.google.com" || h.endsWith(".translate.goog")) {
        const q = u.searchParams.get("u") || u.searchParams.get("url");
        if (q && /^https?:/i.test(q)) return q;
      }
      if (h === "web.archive.org") {
        const m = u.pathname.match(/\/web\/\d+(?:id_)?\/(https?:\/\/.+)/i);
        if (m) return m[1];
      }
      if (h === "webcache.googleusercontent.com") {
        const q = u.searchParams.get("q") || "";
        const m = q.match(/cache:[^:]+:(https?:\/\/\S+)/i) || q.match(/(https?:\/\/\S+)/i);
        if (m) return m[1];
      }
      for (const key of ["url", "u", "q", "target", "dest", "redirect", "r"]) {
        const v = u.searchParams.get(key);
        if (v && /^https?:\/\//i.test(v)) {
          try {
            const inner = new URL(v);
            if (inner.hostname && inner.hostname !== u.hostname) return v;
          } catch (e) {
          }
        }
      }
      if (h.indexOf(".cdn.ampproject.org") >= 0 || h.indexOf(".ampproject.org") >= 0) {
        const parts = u.hostname.split(".");
        if (parts.length > 4) {
          const candidate = parts.slice(0, parts.length - 3).join(".").replace(/--/g, "-");
          if (candidate.indexOf(".") >= 0) return "https://" + candidate + u.pathname;
        }
      }
      return raw;
    } catch (e) {
      return raw;
    }
  }

  // src/blocklist.js
  var SOURCES = [
    {
      name: "stevenblack-porn",
      url: "https://raw.githubusercontent.com/StevenBlack/hosts/master/alternates/porn-only/hosts"
    },
    {
      name: "hagezi-nsfw",
      url: "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/adblock/nsfw.txt"
    }
  ];
  function parseHostsFile(text) {
    const out = [];
    const lines = String(text || "").split(/\r?\n/);
    for (let i = 0; i < lines.length; i++) {
      let line = lines[i].trim();
      if (!line || line[0] === "#") continue;
      let m = line.match(/^(?:0\.0\.0\.0|127\.0\.0\.1)\s+(\S+)/i);
      if (m) {
        out.push(m[1].toLowerCase().replace(/^www\./, ""));
        continue;
      }
      m = line.match(/^\|\|([a-z0-9.-]+)\^/i);
      if (m) out.push(m[1].toLowerCase().replace(/^www\./, ""));
    }
    return out;
  }
  async function loadCachedBlocklist(storage) {
    const key = GATE_V2.blocklist.storageKey;
    try {
      const r = await storage.local.get(key);
      const pack = r[key];
      if (pack && Array.isArray(pack.hosts) && pack.hosts.length) {
        setBlocklistHosts(pack.hosts);
        return { ok: true, size: pack.hosts.length, at: pack.at || 0, hash: pack.hash || "" };
      }
    } catch (e) {
    }
    return { ok: false, size: 0 };
  }
  function simpleHash(s) {
    let h = 0;
    for (let i = 0; i < s.length; i++) h = h * 31 + s.charCodeAt(i) | 0;
    return String(h);
  }
  async function refreshBlocklist(storage, fetchFn) {
    const fetchImpl = fetchFn || fetch;
    const merged = /* @__PURE__ */ new Set();
    const errors = [];
    for (let i = 0; i < SOURCES.length; i++) {
      try {
        const res = await fetchImpl(SOURCES[i].url, { signal: AbortSignal.timeout(45e3) });
        if (!res.ok) {
          errors.push(SOURCES[i].name + ":HTTP" + res.status);
          continue;
        }
        const text = await res.text();
        if (!text || text.length < 200) {
          errors.push(SOURCES[i].name + ":too_small");
          continue;
        }
        if (text.length > 80 * 1024 * 1024) {
          errors.push(SOURCES[i].name + ":too_large");
          continue;
        }
        const hosts = parseHostsFile(text);
        for (let j = 0; j < hosts.length; j++) merged.add(hosts[j]);
      } catch (e) {
        errors.push(SOURCES[i].name + ":" + (e && e.message ? e.message : "fail"));
      }
    }
    if (merged.size < 100) {
      const prev = await loadCachedBlocklist(storage);
      return { ok: false, size: prev.size, errors, keptLastGood: true };
    }
    let list = Array.from(merged);
    if (list.length > GATE_V2.blocklist.maxHosts) list = list.slice(0, GATE_V2.blocklist.maxHosts);
    const hash = simpleHash(list.length + ":" + list[0] + ":" + list[list.length - 1]);
    const pack = { hosts: list, at: Date.now(), hash, errors };
    await storage.local.set({ [GATE_V2.blocklist.storageKey]: pack });
    setBlocklistHosts(list);
    return { ok: true, size: list.length, hash, errors };
  }
  function currentBlocklistSize() {
    return blocklistSize();
  }

  // src/verdict_log.js
  var mem = [];
  var highMem = [];
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
  function pushVerdict(entry) {
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
      thresholds: entry.thresholds || snapshotThresholds()
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
      watchBelow: row.thresholds && row.thresholds.watchBelow
    };
    highMem.unshift(hi);
    while (highMem.length > GATE_V2.log.highScoreMax) highMem.pop();
  }
  function recentVerdicts(n) {
    return mem.slice(0, Math.max(1, n || 100));
  }
  function recentHighScores(n) {
    return highMem.slice(0, Math.max(1, n || 100));
  }
  async function persistVerdicts(storage) {
    if (!storage || !storage.local) return;
    try {
      await storage.local.set({
        [GATE_V2.log.storageKey]: mem.slice(0, 200),
        [GATE_V2.log.highScoreKey]: highMem.slice(0, GATE_V2.log.highScoreMax),
        [GATE_V2.log.thresholdsKey]: snapshotThresholds()
      });
    } catch (e) {
    }
  }
  async function loadVerdicts(storage) {
    if (!storage || !storage.local) return;
    try {
      const r = await storage.local.get([
        GATE_V2.log.storageKey,
        GATE_V2.log.highScoreKey
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
    }
  }

  // src/appeal_v2.js
  var STORAGE_KEY = "gateAppealV2";
  function pathKey(url) {
    try {
      const u = new URL(String(url || ""));
      return u.origin + u.pathname;
    } catch (e) {
      return String(url || "").split("?")[0];
    }
  }
  function todayKey() {
    const d = /* @__PURE__ */ new Date();
    return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0");
  }
  async function loadAppealState(storage) {
    try {
      const r = await storage.local.get(STORAGE_KEY);
      return r[STORAGE_KEY] || { grants: [], queue: [], day: todayKey(), dayCount: 0 };
    } catch (e) {
      return { grants: [], queue: [], day: todayKey(), dayCount: 0 };
    }
  }
  async function saveAppealState(storage, st) {
    await storage.local.set({ [STORAGE_KEY]: st });
  }
  function prune(st, now) {
    st.grants = (st.grants || []).filter((g2) => g2.until > now);
    st.queue = (st.queue || []).filter((q) => q.readyAt > now - GATE_V2.appeal.grantMs);
    if (st.day !== todayKey()) {
      st.day = todayKey();
      st.dayCount = 0;
    }
    return st;
  }
  function isPathGranted(st, url, now) {
    const key = pathKey(url);
    const t = now || Date.now();
    for (let i = 0; i < (st.grants || []).length; i++) {
      if (st.grants[i].key === key && st.grants[i].until > t) return true;
    }
    return false;
  }
  async function handleAppealRequest(storage, msg) {
    const now = Date.now();
    let st = prune(await loadAppealState(storage), now);
    const url = String(msg.url || "");
    const host = String(msg.host || "").toLowerCase().replace(/^www\./, "");
    const tier = hostTier(host);
    if (tier === "PORN_STRICT") {
      return { allow: false, why: "Known adult host \u2014 appeal denied." };
    }
    const q = webSearchQuery(url);
    if (tier === "TRUSTED" && !urlHasAdultTokens(url, msg.title || "")) {
      const until = now + GATE_V2.appeal.grantMs;
      st.grants.push({ key: pathKey(url), until, host, by: "trusted" });
      await saveAppealState(storage, st);
      return { allow: true, why: "Trusted host \u2014 opened for this path.", until };
    }
    if (tier === "SEARCH" && q !== null) {
      const adult = adultTokensInText(q).length > 0;
      if (!adult || isRecoverySearchQuery(q)) {
        const until = now + GATE_V2.appeal.grantMs;
        st.grants.push({ key: pathKey(url), until, host, by: "search" });
        await saveAppealState(storage, st);
        return {
          allow: true,
          why: adult ? "Recovery search \u2014 opened." : "Search query is not adult \u2014 opened.",
          until
        };
      }
    }
    if (st.dayCount >= GATE_V2.appeal.maxGrantsPerDay) {
      return { allow: false, why: "Daily appeal grant limit reached. Try tomorrow." };
    }
    const key = pathKey(url);
    const existing = (st.queue || []).find((x) => x.key === key);
    if (existing && existing.readyAt > now) {
      return {
        allow: false,
        queued: true,
        readyAt: existing.readyAt,
        why: "Review queued \u2014 available in " + Math.ceil((existing.readyAt - now) / 6e4) + " min. Note text cannot skip the wait."
      };
    }
    if (existing && existing.readyAt <= now) {
      if (st.dayCount >= GATE_V2.appeal.maxGrantsPerDay) {
        return { allow: false, why: "Daily appeal grant limit reached." };
      }
      const until = now + GATE_V2.appeal.grantMs;
      st.grants.push({ key, until, host, by: "cooldown" });
      st.queue = st.queue.filter((x) => x.key !== key);
      st.dayCount = (st.dayCount || 0) + 1;
      await saveAppealState(storage, st);
      return {
        allow: true,
        why: "Cooldown finished \u2014 30 min path grant. Logged.",
        until
      };
    }
    const readyAt = now + GATE_V2.appeal.cooldownMs;
    st.queue.push({ key, host, readyAt, note: String(msg.note || "").slice(0, 80) });
    await saveAppealState(storage, st);
    return {
      allow: false,
      queued: true,
      readyAt,
      why: "Queued for review (" + Math.round(GATE_V2.appeal.cooldownMs / 6e4) + " min). Your note is logged but cannot unlock early."
    };
  }

  // src/sw_v2.js
  var navState = /* @__PURE__ */ new Map();
  function ext() {
    return typeof chrome !== "undefined" ? chrome : browser;
  }
  async function gateV2Init() {
    const api = ext();
    await loadSavedThresholds(api.storage);
    await saveThresholds(api.storage, snapshotThresholds());
    await loadVerdicts(api.storage);
    await loadCachedBlocklist(api.storage);
    try {
      await ensureOffscreen();
    } catch (e) {
    }
    api.alarms.create("gateV2Blocklist", { periodInMinutes: GATE_V2.blocklist.alarmMinutes });
    api.alarms.onAlarm.addListener((a) => {
      if (a.name === "gateV2Blocklist") {
        void refreshBlocklist(api.storage).then(() => persistVerdicts(api.storage));
      }
    });
    void syncSafeSearchDnr(api);
    setTimeout(() => {
      void refreshBlocklist(api.storage);
    }, 15e3);
  }
  async function syncSafeSearchDnr(api) {
    if (!api.declarativeNetRequest || !api.declarativeNetRequest.updateDynamicRules) return;
    const base = 9600;
    const removeRuleIds = [];
    for (let i = 0; i < 12; i++) removeRuleIds.push(base + i);
    const addRules = [
      {
        id: base,
        priority: 50,
        action: {
          type: "redirect",
          redirect: { transform: { queryTransform: { addOrReplaceParams: [{ key: "safe", value: "active" }] } } }
        },
        condition: {
          urlFilter: "||google.",
          resourceTypes: ["main_frame"],
          requestDomains: ["google.com", "www.google.com", "google.co.in"]
        }
      },
      {
        id: base + 1,
        priority: 50,
        action: {
          type: "redirect",
          redirect: { transform: { queryTransform: { addOrReplaceParams: [{ key: "adlt", value: "strict" }] } } }
        },
        condition: { requestDomains: ["bing.com", "www.bing.com"], resourceTypes: ["main_frame"] }
      },
      {
        id: base + 2,
        priority: 50,
        action: {
          type: "redirect",
          redirect: { transform: { queryTransform: { addOrReplaceParams: [{ key: "kp", value: "1" }] } } }
        },
        condition: {
          requestDomains: ["duckduckgo.com", "www.duckduckgo.com"],
          resourceTypes: ["main_frame"]
        }
      },
      {
        id: base + 3,
        priority: 50,
        action: {
          type: "modifyHeaders",
          requestHeaders: [{ header: "YouTube-Restrict", operation: "set", value: "Strict" }]
        },
        condition: {
          requestDomains: ["youtube.com", "www.youtube.com", "m.youtube.com"],
          resourceTypes: ["main_frame", "sub_frame", "xmlhttprequest"]
        }
      }
    ];
    try {
      await api.declarativeNetRequest.updateDynamicRules({ removeRuleIds, addRules });
    } catch (e) {
      console.warn("CALT Gate SafeSearch DNR", e);
    }
  }
  async function ensureOffscreen() {
    const api = ext();
    if (!api.offscreen) return;
    const exists = await api.offscreen.hasDocument?.();
    if (exists) return;
    try {
      await api.offscreen.createDocument({
        url: "offscreen.html",
        reasons: ["BLOBS"],
        justification: "NSFWJS WASM image classification for CALT Gate"
      });
    } catch (e) {
    }
  }
  function gateV2OnNav(tabId, url, title, meta) {
    if (!GATE_V2.enabled) return null;
    const dest = unwrapDestinationUrl(url);
    const st = navState.get(tabId) || { navId: 0, images: {} };
    st.navId = (st.navId || 0) + 1;
    const navId = st.navId;
    st.url = dest;
    st.images = {};
    st.scoreVerdict = null;
    navState.set(tabId, st);
    return evaluateTab(tabId, navId, dest, title || "", meta || {});
  }
  async function evaluateTab(tabId, navId, url, title, meta) {
    const api = ext();
    const st = navState.get(tabId);
    if (!st || st.navId !== navId) return null;
    let appealGrant = false;
    try {
      const ast = await loadAppealState(api.storage);
      appealGrant = isPathGranted(ast, url, Date.now());
    } catch (e) {
      appealGrant = false;
    }
    const input = {
      url,
      title,
      mode: meta.mode || "free",
      softlandEnabled: meta.softlandEnabled !== false,
      nativeReason: meta.nativeReason || "",
      contentScore: meta.contentScore || 0,
      contentAction: meta.contentAction || "",
      rta: Boolean(meta.rta),
      metaAdult: Boolean(meta.metaAdult),
      images: st.images || {},
      quinn: meta.quinn || null,
      appealGrant,
      priorScore: st.scoreVerdict && st.scoreVerdict.score || 0
    };
    let verdict = decide(input);
    if (st.scoreVerdict) verdict = mergeMonotonic(st.scoreVerdict, verdict);
    st.scoreVerdict = verdict;
    navState.set(tabId, st);
    pushVerdict({
      ...verdict,
      mode: input.mode || "",
      thresholds: snapshotThresholds()
    });
    void persistVerdicts(api.storage);
    if (GATE_V2.shadowOnly) return { ...verdict, shadow: true };
    if (verdict.verdict === "LOCK") {
      return verdict;
    }
    if (verdict.verdict === "WATCH") {
      try {
        api.tabs.sendMessage(tabId, {
          type: "GATE_V2_WATCH",
          blur: true,
          scannerOffline: Boolean((st.images || {}).scannerOffline),
          navId
        });
      } catch (e) {
      }
    }
    return verdict;
  }
  async function gateV2OnImageSignal(tabId, payload) {
    const st = navState.get(tabId);
    if (!st) return null;
    if (payload.navId != null && payload.navId !== st.navId) return null;
    const img = st.images || {};
    img.maxPornHentai = Math.max(img.maxPornHentai || 0, Number(payload.pornHentai) || 0);
    img.maxSexy = Math.max(img.maxSexy || 0, Number(payload.sexy) || 0);
    if ((Number(payload.pornHentai) || 0) >= GATE_V2.image.aggregateMin) {
      img.count035 = (img.count035 || 0) + 1;
    }
    if (payload.scannerOffline) img.scannerOffline = true;
    st.images = img;
    navState.set(tabId, st);
    return evaluateTab(tabId, st.navId, st.url || payload.url || "", payload.title || "", {
      mode: payload.mode,
      softlandEnabled: payload.softlandEnabled,
      nativeReason: payload.nativeReason
    });
  }
  async function gateV2OnContentScore(tabId, payload) {
    const st = navState.get(tabId);
    if (!st) return null;
    return evaluateTab(tabId, st.navId, payload.url || st.url || "", payload.title || "", {
      mode: payload.mode,
      softlandEnabled: payload.softlandEnabled,
      nativeReason: payload.nativeReason,
      contentScore: payload.score,
      contentAction: payload.action,
      rta: payload.rta,
      metaAdult: payload.metaAdult
    });
  }
  function gateV2WhyLabel(hard, reasons) {
    const h = String(hard || "").toLowerCase();
    if (h === "porn_strict" || h === "adult_search_query" || h === "rta") return "porn";
    if (h.indexOf("porn") >= 0) return "porn";
    if (h === "watch_study") return "study";
    if (h === "sensitive") return "sensitive";
    if (h.indexOf("morning") >= 0 || h === "bible" || h === "plan" || h.indexOf("native_") === 0)
      return h.replace(/^native_/, "") || "morning";
    if (h === "incubation") return "incubation";
    if (h) return h;
    const joined = (reasons || []).join(" ").toLowerCase();
    if (joined.indexOf("image_porn") >= 0 || joined.indexOf("content_lock") >= 0) return "keyword";
    if (joined.indexOf("watch") >= 0 || joined.indexOf("study") >= 0) return "study";
    return "blocked";
  }
  function gateV2LockedUrl(extensionBase, fromUrl, reasons, hard) {
    const u = new URL(extensionBase + "locked.html");
    try {
      const parsed = new URL(fromUrl);
      u.searchParams.set("host", parsed.hostname.replace(/^www\./, ""));
    } catch (e) {
      u.searchParams.set("host", "");
    }
    u.searchParams.set("from", String(fromUrl || "").slice(0, 1800));
    u.searchParams.set("why", gateV2WhyLabel(hard, reasons));
    u.searchParams.set("v2", (reasons || []).slice(0, 8).join("|"));
    return u.toString();
  }
  async function gateV2HandleMessage(msg, sender, sendResponse) {
    const api = ext();
    if (!msg || !msg.type) return false;
    if (msg.type === "GATE_V2_STATUS") {
      sendResponse({
        ok: true,
        version: GATE_V2.version,
        enabled: GATE_V2.enabled,
        shadowOnly: GATE_V2.shadowOnly,
        blocklist: currentBlocklistSize(),
        thresholds: snapshotThresholds(),
        recent: recentVerdicts(20),
        highScores: recentHighScores(20)
      });
      return true;
    }
    if (msg.type === "GATE_V2_WHY") {
      sendResponse({
        ok: true,
        recent: recentVerdicts(100),
        highScores: recentHighScores(100),
        thresholds: snapshotThresholds()
      });
      return true;
    }
    if (msg.type === "GATE_V2_THRESHOLDS_GET") {
      sendResponse({ ok: true, thresholds: snapshotThresholds() });
      return true;
    }
    if (msg.type === "GATE_V2_THRESHOLDS_SET") {
      void saveThresholds(api.storage, msg.thresholds || msg).then((snap) => {
        sendResponse({ ok: true, thresholds: snap });
      });
      return true;
    }
    if (msg.type === "GATE_V2_REFRESH_BLOCKLIST") {
      void refreshBlocklist(api.storage).then((r) => sendResponse(r));
      return true;
    }
    if (msg.type === "PORN_APPEAL_REQUEST" || msg.type === "GATE_V2_APPEAL") {
      void handleAppealRequest(api.storage, msg).then((r) => sendResponse(r));
      return true;
    }
    if (msg.type === "GATE_V2_IMAGE_RESULT") {
      const tabId = sender.tab && sender.tab.id;
      void gateV2OnImageSignal(tabId, msg).then((v) => {
        if (v && v.verdict === "LOCK" && !GATE_V2.shadowOnly && typeof tabId === "number") {
          const target = gateV2LockedUrl(
            api.runtime.getURL(""),
            msg.url || (navState.get(tabId) || {}).url,
            v.reasons,
            v.hard
          );
          void api.tabs.update(tabId, { url: target });
        }
        sendResponse({ ok: true, verdict: v });
      });
      return true;
    }
    if (msg.type === "GATE_V2_CONTENT") {
      const tabId = sender.tab && sender.tab.id;
      void gateV2OnContentScore(tabId, msg).then((v) => {
        if (v && v.verdict === "LOCK" && !GATE_V2.shadowOnly && typeof tabId === "number") {
          const target = gateV2LockedUrl(api.runtime.getURL(""), msg.url || "", v.reasons, v.hard);
          void api.tabs.update(tabId, { url: target });
        }
        sendResponse({ ok: true, verdict: v });
      });
      return true;
    }
    if (msg.type === "GATE_V2_CLASSIFY_URL") {
      void (async () => {
        await ensureOffscreen();
        try {
          const resp = await api.runtime.sendMessage({
            type: "OFFSCREEN_NSFW_CLASSIFY",
            dataUrl: msg.dataUrl,
            url: msg.url
          });
          sendResponse(resp);
        } catch (e) {
          sendResponse({ ok: false, scannerOffline: true, error: String(e && e.message) });
        }
      })();
      return true;
    }
    return false;
  }
  function gateV2ShouldBlurHost(host) {
    const t = hostTier(host);
    return t === "UGC" || t === "UNKNOWN" || t === "SOCIAL" || t === "SENSITIVE" || t === "WATCH";
  }
  function gateV2WatchOnLoad(url) {
    return isImageSearchUrl(url) || gateV2ShouldBlurHost((() => {
      try {
        return new URL(url).hostname;
      } catch (e) {
        return "";
      }
    })());
  }

  // src/v2_globals.js
  var g = typeof globalThis !== "undefined" ? globalThis : self;
  g.CaltGateV2 = {
    GATE_V2,
    init: gateV2Init,
    onNav: gateV2OnNav,
    handleMessage: gateV2HandleMessage,
    lockedUrl: gateV2LockedUrl,
    watchOnLoad: gateV2WatchOnLoad,
    decide,
    hostTier,
    unwrap: unwrapDestinationUrl
  };
})();

// ----- end v2_bundle.js -----

// ----- begin background.js -----
// ============================================================
// CALT Gate — blocker only (MV3 DNR + light softLand)
// No telemetry / WebSocket. Pair with CALT SelfTracker.
// Watch hosts = DNR only. Other blocks = one-tab softLand.
// ============================================================

/* global GATE_API_URL, GATE_ALERT_URL, GATE_EXT_LOG_URL, GATE_ALERT_GAP_MS, FORCE_WATCH_HOSTS,
   FORCE_PORN_HOSTS, isStrictDayMode, TEMP_ALLOW_STORAGE_KEY, TEMP_ALLOW_MS, pruneTempAllows,
   buildTempAllowGrant, upsertTempAllow, isTempAllowExcludedHost, isHostTempAllowed,
   tempAllowUntilForHost, browserPolicyOrFallback, shouldBlockUrl, dnrHostList, listMatch,
   redirectTargetUrl, isCaltSpaUrl, blockKindForUrl, hostnameFromUrl,
   isForceWatchHost, isForcePornHost, classifyHostCategory, isExtensionOrInternalUrl,
   applyContentScoreFromPolicy, CALT_BIBLE_URL, CALT_PRODUCTIVITY_URL, isFreeDay,
   APPEAL_ALLOW_MS, APPEAL_ALLOW_STORAGE_KEY, QWEN_DEFAULT_BASE, QWEN_DEFAULT_MODEL,
   isAppealHardDenyHost, pruneAppealAllows, isHostAppealAllowed, upsertAppealAllow,
   isPornAppealReason, normalizeHost */

// Shared helpers are prepended by scripts/build_extension_workers.ps1 into service_worker.js

var DNR_WATCH_RULE_BASE = 9200;
/** Cap DNR rules — SoftLand covers any overflow from server watch_domains. */
var MAX_DNR_WATCH_HOSTS = 16;
/** Hard-block distraction hosts in every mode (including FREE). */
var DNR_PORN_RULE_BASE = 9300;
var MAX_DNR_PORN_HOSTS = 80;
/** Default poll; overwritten from browser.intervals.extension_gate_poll_s. */
var GATE_POLL_MS =
  typeof GATE_POLL_ACTIVE_S === "number" ? Math.max(4000, GATE_POLL_ACTIVE_S * 1000) : 4000;
var SOFTLAND_DEDUP_MS = 15000;

var extAPI = typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;

var gateCache = null;
var redirectsEnabled = true;
var lastGateFetchAt = 0;
var lastDnrFingerprint = "";
var lastDnrPornFingerprint = "";
/** Hosts currently covered by active DNR — SoftLand skips these only. */
var activeDnrWatchHosts = [];
var activeDnrPornHosts = [];
var lastAlertAt = 0;
var softLandDone = Object.create(null);
var softLandInFlight = Object.create(null);
var tempAllowsCache = [];
var appealAllowsCache = [];
var lastGateModeKey = "";
var softLandRecentAt = [];
var SOFTLAND_STORM_WINDOW_MS = 8000;
var SOFTLAND_STORM_MAX = 6;
var REDIRECT_COOLDOWN_MS = 30000;
var redirectCooldownUntil = 0;
var lastNotifyAt = 0;
var NOTIFY_GAP_MS = 8000;
/** Study :8000 side-channels (WS / alerts / HTTP gate). Default OFF. */
var studyHttpFallback = false;
/** Last native host health (ping / get_mode / get_softland_doc). */
var nativeHostOk = null;
/**
 * Secret Inspect mode — pause watch/social SoftLand redirects + watch DNR so overlays
 * can score pages. Porn / hard-deny hosts still lock (Inspect must not open tubes).
 */
var inspectModeCached = false;
/** Quinn appeals — Focus Home owns on/off via quinn_dashboard.json. Default ON. */
var quinnEnabledCached = true;
var lastQuinnSyncAt = 0;
var GET_MODE_TTL_MS = 2500;
var getModeCache = Object.create(null);
var nativePort = null;
var nativeQueue = [];
var nativeInFlight = false;
var gateBootstrapped = false;

var llmHostPrefs = {
  baseUrl: typeof QWEN_DEFAULT_BASE !== "undefined" ? QWEN_DEFAULT_BASE : "http://127.0.0.1:11434/v1",
  model: typeof QWEN_DEFAULT_MODEL !== "undefined" ? QWEN_DEFAULT_MODEL : "qwen2.5:3b",
  apiKey: "",
};

extAPI.storage.local.get(
  [
    "gateCache",
    "redirectsEnabled",
    "tempAllows",
    "appealAllows",
    "caltGateHttpFallback",
    "caltSoftlandHttpFallback",
    "caltInspectMode",
    "caltLlmHost",
    "caltQuinnEnabled",
  ],
  function (result) {
    if (result.gateCache) gateCache = result.gateCache;
    redirectsEnabled = true;
    if (result.redirectsEnabled === false) extAPI.storage.local.set({ redirectsEnabled: true });
    if (Array.isArray(result.tempAllows)) {
      tempAllowsCache =
        typeof pruneTempAllows === "function" ? pruneTempAllows(result.tempAllows, Date.now()) : result.tempAllows;
    }
    if (Array.isArray(result.appealAllows)) {
      appealAllowsCache =
        typeof pruneAppealAllows === "function"
          ? pruneAppealAllows(result.appealAllows, Date.now())
          : result.appealAllows;
    }
    // Focus product lock: never use Study :8000 SoftLand/gate HTTP (native msg_host only).
    studyHttpFallback = false;
    if (result.caltGateHttpFallback || result.caltSoftlandHttpFallback) {
      extAPI.storage.local.set({ caltGateHttpFallback: false, caltSoftlandHttpFallback: false });
    }
    inspectModeCached = Boolean(result.caltInspectMode);
    if (typeof result.caltQuinnEnabled === "boolean") quinnEnabledCached = result.caltQuinnEnabled;
    if (result.caltLlmHost && typeof result.caltLlmHost === "object") {
      llmHostPrefs = Object.assign({}, llmHostPrefs, result.caltLlmHost);
    }
    if (typeof CaltGateV2 !== "undefined" && CaltGateV2.init) {
      void CaltGateV2.init();
    }
  }
);

/**
 * Pull Quinn on/off from Focus mirror (msg_host get_quinn_prefs).
 * Gate popup no longer writes this — SoftLand blocking stays here; appeals toggle is Focus.
 */
async function syncQuinnFromFocus(opts) {
  opts = opts || {};
  var now = Date.now();
  if (!opts.force && lastQuinnSyncAt && now - lastQuinnSyncAt < 2000) {
    return quinnEnabledCached !== false;
  }
  lastQuinnSyncAt = now;
  try {
    var res = await nativeCall({ type: "get_quinn_prefs" }, 800);
    var en = true;
    if (res && typeof res.appeals_enabled === "boolean") {
      en = res.appeals_enabled;
    } else if (res && res.document && typeof res.document === "object" &&
               typeof res.document.appeals_enabled === "boolean") {
      en = res.document.appeals_enabled;
    } else if (res && typeof res.document === "string") {
      try {
        var doc = JSON.parse(res.document);
        if (typeof doc.appeals_enabled === "boolean") en = doc.appeals_enabled;
      } catch (eParse) {
        /* keep default */
      }
    } else if (res && typeof res.ok === "boolean" && res.ok === false) {
      return quinnEnabledCached !== false;
    }
    quinnEnabledCached = en !== false;
    try {
      extAPI.storage.local.set({ caltQuinnEnabled: quinnEnabledCached });
    } catch (eStore) {
      /* ignore */
    }
  } catch (e) {
    /* keep cached */
  }
  return quinnEnabledCached !== false;
}

function lockedPageUrl() {
  return extAPI.runtime.getURL("locked.html");
}

/** True if this tab is already on our lock interstitial (any query string). */
function tabUrlIsLockedPage(url) {
  return Boolean(url && String(url).indexOf("locked.html") >= 0);
}

/**
 * Hard-nav to lock page only when SoftLand skipped us AND the tab is not already locked.
 * Without this, softLandBlockedTab returns false on locked.html and callers re-tabs.update
 * with a different ?why= → infinite reload flicker.
 */
async function forceNavigateToLock(tabId, target) {
  if (tabId == null || !target) return false;
  try {
    var cur = await extAPI.tabs.get(tabId);
    var cu = cur && cur.url ? String(cur.url) : "";
    if (tabUrlIsLockedPage(cu)) return false;
    await extAPI.tabs.update(tabId, { url: target });
    return true;
  } catch (e) {
    return false;
  }
}

function lockedPageUrlForBlocked(blockedUrl, reason, untilIso) {
  var base = lockedPageUrl();
  var host = "";
  try {
    host = typeof hostnameFromUrl === "function" ? hostnameFromUrl(blockedUrl) : "";
  } catch (e) {
    host = "";
  }
  if (!host && !reason && !untilIso) return base;
  var q = host ? "host=" + encodeURIComponent(host) : "";
  try {
    if (blockedUrl && String(blockedUrl).indexOf("http") === 0) {
      q += (q ? "&" : "") + "from=" + encodeURIComponent(String(blockedUrl).slice(0, 500));
    }
    if (reason) {
      q += (q ? "&" : "") + "why=" + encodeURIComponent(String(reason).slice(0, 80));
    }
    // SoftLand get_mode until (ISO) — native path; not temp-allow until.
    if (untilIso) {
      q += (q ? "&" : "") + "until=" + encodeURIComponent(String(untilIso).slice(0, 40));
    }
  } catch (e2) {
    /* ignore */
  }
  return q ? base + "?" + q : base;
}

function gateCacheForBlockCheck() {
  if (!gateCache) return null;
  return Object.assign({}, gateCache, { temp_allows: tempAllowsCache, tempAllows: tempAllowsCache });
}

function reportGateAlert(kind, detail) {
  if (!studyHttpFallback) return;
  var now = Date.now();
  if (now - lastAlertAt < (GATE_ALERT_GAP_MS || 45000)) return;
  lastAlertAt = now;
  try {
    fetch(GATE_ALERT_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ kind: kind || "generic_rule_break", detail: detail || "" }),
      cache: "no-store",
    }).catch(function () {});
  } catch (e) {
    /* ignore */
  }
}

function logExtensionEvent(event, payload) {
  if (!studyHttpFallback) return;
  var url = typeof GATE_EXT_LOG_URL !== "undefined" ? GATE_EXT_LOG_URL : "";
  if (!url) return;
  try {
    fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(
        Object.assign({ event: event || "unknown", source: "calt-gate" }, payload || {})
      ),
      cache: "no-store",
    }).catch(function () {});
  } catch (e) {
    /* ignore */
  }
}

function invalidateGetModeCache(hostHint) {
  if (!hostHint) {
    getModeCache = Object.create(null);
    return;
  }
  var h = String(hostHint).toLowerCase().replace(/^www\./, "");
  Object.keys(getModeCache).forEach(function (k) {
    if (k.indexOf(h) >= 0) delete getModeCache[k];
  });
}

function getModeCacheKey(url) {
  try {
    var u = new URL(String(url || ""));
    return (u.hostname + u.pathname).toLowerCase().replace(/^www\./, "").slice(0, 220);
  } catch (e) {
    return String(url || "").slice(0, 220);
  }
}

function failNativeQueue() {
  nativeInFlight = false;
  try {
    if (nativePort) nativePort.disconnect();
  } catch (e) {}
  nativePort = null;
  nativeHostOk = false;
  var q = nativeQueue.splice(0, nativeQueue.length);
  for (var i = 0; i < q.length; i++) {
    try {
      clearTimeout(q[i].timer);
    } catch (e2) {}
    q[i].resolve(null);
  }
}

function pumpNativeQueue() {
  if (nativeInFlight || !nativeQueue.length) return;
  if (!extAPI.runtime || typeof extAPI.runtime.connectNative !== "function") {
    failNativeQueue();
    return;
  }
  var job = nativeQueue[0];
  nativeInFlight = true;
  try {
    if (!nativePort) {
      nativePort = extAPI.runtime.connectNative("com.calt.msg_host");
      // connectNative sets lastError when the host is already gone.
      var connectErr = extAPI.runtime.lastError;
      if (connectErr && connectErr.message) nativeHostOk = false;
      nativePort.onDisconnect.addListener(function () {
        // "Native host has exited" is lastError. Reading .message marks it checked.
        var discErr = extAPI.runtime && extAPI.runtime.lastError;
        if (discErr && discErr.message) nativeHostOk = false;
        if (!nativeInFlight && !nativeQueue.length) {
          nativePort = null;
          return;
        }
        failNativeQueue();
      });
    }
  } catch (e) {
    nativeQueue.shift();
    nativeInFlight = false;
    nativeHostOk = false;
    job.resolve(null);
    pumpNativeQueue();
    return;
  }
  var settled = false;
  var onMsg = null;
  function finish(msg, killPort) {
    if (settled) return;
    settled = true;
    try {
      clearTimeout(job.timer);
    } catch (e2) {}
    if (onMsg && nativePort) {
      try {
        nativePort.onMessage.removeListener(onMsg);
      } catch (e3) {}
    }
    if (nativeQueue[0] === job) nativeQueue.shift();
    nativeInFlight = false;
    if (killPort) {
      try {
        if (nativePort) nativePort.disconnect();
      } catch (e4) {}
      nativePort = null;
    }
    if (msg && typeof msg === "object") nativeHostOk = msg.ok !== false;
    else if (killPort) nativeHostOk = false;
    job.resolve(msg && typeof msg === "object" ? msg : null);
    pumpNativeQueue();
  }
  job.timer = setTimeout(function () {
    finish(null, true);
  }, job.timeoutMs || 1200);
  try {
    onMsg = function (msg) {
      finish(msg, false);
    };
    nativePort.onMessage.addListener(onMsg);
    nativePort.postMessage(job.message);
  } catch (e5) {
    finish(null, true);
  }
}

function nativeCall(message, timeoutMs) {
  return new Promise(function (resolve) {
    nativeQueue.push({
      message: message,
      timeoutMs: timeoutMs || 1200,
      resolve: resolve,
      timer: null,
    });
    pumpNativeQueue();
  });
}

async function softlandNativePing() {
  var msg = await nativeCall({ type: "ping", schema_version: 1 }, 800);
  nativeHostOk = Boolean(msg && msg.ok && msg.type === "pong");
  return nativeHostOk;
}

function redirectsPausedByCircuit() {
  return redirectCooldownUntil && Date.now() < redirectCooldownUntil;
}

function noteSoftLandAttempt() {
  var now = Date.now();
  softLandRecentAt.push(now);
  while (softLandRecentAt.length && now - softLandRecentAt[0] > SOFTLAND_STORM_WINDOW_MS) {
    softLandRecentAt.shift();
  }
  if (softLandRecentAt.length >= SOFTLAND_STORM_MAX) {
    redirectCooldownUntil = now + REDIRECT_COOLDOWN_MS;
    softLandRecentAt = [];
    logExtensionEvent("circuit_breaker", {
      detail: "softLand storm — redirects paused " + Math.round(REDIRECT_COOLDOWN_MS / 1000) + "s",
      notify: false,
    });
    console.warn("CALT Gate: softLand circuit breaker — redirects paused");
    return true;
  }
  return false;
}

function showBlockNotification(title, message) {
  var now = Date.now();
  if (now - lastNotifyAt < NOTIFY_GAP_MS) return;
  lastNotifyAt = now;
  if (!extAPI.notifications || !extAPI.notifications.create) return;
  try {
    var noteIcon = extAPI.runtime.getURL("icon.png");
    var noted = extAPI.notifications.create(
      "calt-gate-" + now,
      {
        type: "basic",
        iconUrl: noteIcon,
        title: title || "CALT blocked a webpage",
        message: message || "This site is SoftLand-blocked. Edge stayed open.",
        priority: 2,
        requireInteraction: false,
      },
      function () {
        var noteErr = extAPI.runtime && extAPI.runtime.lastError;
        if (noteErr && noteErr.message) return;
      }
    );
    // Edge returns a promise even with a callback. A missing icon rejects it.
    if (noted && typeof noted.then === "function") {
      noted.then(
        function () {},
        function () {}
      );
    }
  } catch (e) {
    /* ignore */
  }
}

function watchBlockActive() {
  if (!redirectsEnabled) return false;
  if (!gateCache || (!gateCache.ok && !gateCache.degraded)) return false;
  // Reward day / goal unlock must clear YouTube DNR (softLand alone is not enough).
  if (typeof isFreeDay === "function" && isFreeDay(gateCache)) return false;
  var browser = gateCache.browser || {};
  var mode = String(browser.mode || "").toLowerCase();
  if (mode === "free" || gateCache.day_unlimited || gateCache.reward_day || gateCache.unlocked) {
    return false;
  }
  if (browser.block_watch_sites) return true;
  return typeof isStrictDayMode === "function" && isStrictDayMode(mode);
}

async function syncDeclarativeWatchBlock(opts) {
  opts = opts || {};
  if (!extAPI.declarativeNetRequest || !extAPI.declarativeNetRequest.updateDynamicRules) {
    return;
  }
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var hosts =
    typeof dnrHostList === "function"
      ? dnrHostList(pol.force_watch_hosts || pol.watch_domains, pol.watch_domains, MAX_DNR_WATCH_HOSTS)
      : (pol.watch_domains || FORCE_WATCH_HOSTS || []).slice(0, MAX_DNR_WATCH_HOSTS);
  var active = watchBlockActive() && !inspectModeCached;
  var fingerprint =
    (active ? "1" : "0") + "|inspect:" + (inspectModeCached ? "1" : "0") + "|" + hosts.join(",");
  if (!opts.force && fingerprint === lastDnrFingerprint) return;

  var removeIds = [];
  for (var i = 0; i < MAX_DNR_WATCH_HOSTS + 4; i++) removeIds.push(DNR_WATCH_RULE_BASE + i);

  try {
    if (!active) {
      await extAPI.declarativeNetRequest.updateDynamicRules({
        removeRuleIds: removeIds,
        addRules: [],
      });
      lastDnrFingerprint = fingerprint;
      activeDnrWatchHosts = [];
      return;
    }
    var target = lockedPageUrl();
    var addRules = [];
    for (var j = 0; j < hosts.length; j++) {
      addRules.push({
        id: DNR_WATCH_RULE_BASE + j,
        priority: 100,
        action: { type: "redirect", redirect: { url: target } },
        condition: {
          requestDomains: [hosts[j]],
          resourceTypes: ["main_frame"],
        },
      });
    }
    await extAPI.declarativeNetRequest.updateDynamicRules({
      removeRuleIds: removeIds,
      addRules: addRules,
    });
    lastDnrFingerprint = fingerprint;
    activeDnrWatchHosts = hosts.slice();
  } catch (e) {
    console.warn("CALT Gate: DNR watch sync failed", e);
  }
}

/** Always-on DNR for distraction hosts (FREE + study). SoftLand is backup only. */
async function syncDeclarativePornBlock(opts) {
  opts = opts || {};
  if (!extAPI.declarativeNetRequest || !extAPI.declarativeNetRequest.updateDynamicRules) {
    return;
  }
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  // forcePornList unions SoftLand payload + offline seeds (sxyprn, etc.).
  var pornSeed =
    typeof forcePornList === "function"
      ? forcePornList(pol)
      : pol.force_porn_hosts || pol.porn_domains || FORCE_PORN_HOSTS || [];
  var hosts =
    typeof dnrHostList === "function"
      ? dnrHostList(
          typeof FORCE_SENSITIVE_HOSTS !== "undefined" ? FORCE_SENSITIVE_HOSTS : [],
          pornSeed,
          MAX_DNR_PORN_HOSTS
        )
      : pornSeed.slice(0, MAX_DNR_PORN_HOSTS);
  // Porn has no exceptions — do not strip DNR hosts for Quinn appeal grants.
  appealAllowsCache =
    typeof pruneAppealAllows === "function"
      ? pruneAppealAllows(appealAllowsCache, Date.now())
      : appealAllowsCache || [];
  // Porn DNR is always on (commitment). Inspect + "Toggle redirects" only pause
  // SoftLand watch/social redirects — not adult host DNR.
  var active = true;
  var appealFp = appealAllowsCache
    .map(function (e) {
      return e.host + "@" + e.until;
    })
    .join(",");
  var fingerprint =
    (active ? "1" : "0") +
    "|porn|inspect:" +
    (inspectModeCached ? "1" : "0") +
    "|appeal:" +
    appealFp +
    "|" +
    hosts.join(",");
  if (!opts.force && fingerprint === lastDnrPornFingerprint) return;

  var removeIds = [];
  for (var i = 0; i < MAX_DNR_PORN_HOSTS + 4; i++) removeIds.push(DNR_PORN_RULE_BASE + i);

  try {
    if (!active) {
      await extAPI.declarativeNetRequest.updateDynamicRules({
        removeRuleIds: removeIds,
        addRules: [],
      });
      lastDnrPornFingerprint = fingerprint;
      activeDnrPornHosts = [];
      return;
    }
    var target = lockedPageUrl();
    var addRules = [];
    for (var j = 0; j < hosts.length; j++) {
      addRules.push({
        id: DNR_PORN_RULE_BASE + j,
        priority: 110,
        action: { type: "redirect", redirect: { url: target } },
        condition: {
          requestDomains: [hosts[j]],
          resourceTypes: ["main_frame"],
        },
      });
    }
    await extAPI.declarativeNetRequest.updateDynamicRules({
      removeRuleIds: removeIds,
      addRules: addRules,
    });
    lastDnrPornFingerprint = fingerprint;
    activeDnrPornHosts = hosts.slice();
  } catch (e) {
    console.warn("CALT Gate: DNR distraction sync failed", e);
  }
}

function hostCoveredByActiveDnr(host) {
  if (!host) return false;
  if (typeof listMatch === "function") {
    if (activeDnrWatchHosts.length && listMatch(host, activeDnrWatchHosts)) return true;
    if (activeDnrPornHosts.length && listMatch(host, activeDnrPornHosts)) return true;
    return false;
  }
  return false;
}

function caltExtensionHeaders() {
  var mode = "";
  try {
    mode = String((gateCache && gateCache.browser && gateCache.browser.mode) || "");
  } catch (e) {
    mode = "";
  }
  var paused = false;
  try {
    paused = typeof redirectsPausedByCircuit === "function" && redirectsPausedByCircuit();
  } catch (e2) {
    paused = false;
  }
  return {
    "X-CALT-Extension": "calt-gate",
    "X-CALT-Ext-Mode": mode,
    "X-CALT-Ext-Circuit": paused ? "1" : "0",
    "X-CALT-Ext-Paused": redirectsEnabled === false ? "1" : "0",
  };
}

var gateFetchBackoffMs = 0;
var gateNotifyWs = null;

function noteGateFetchSuccess() {
  gateFetchBackoffMs = 0;
}

function noteGateFetchFailure() {
  if (!gateFetchBackoffMs) gateFetchBackoffMs = 30000;
  else gateFetchBackoffMs = Math.min(300000, gateFetchBackoffMs * 2);
  lastGateFetchAt = Date.now();
}

function connectGateNotifyWs() {
  // Study gate notify WS — opt-in only (same flag as HTTP distraction-gate).
  if (!studyHttpFallback) return;
  var url = typeof GATE_NOTIFY_WS_URL !== "undefined" ? GATE_NOTIFY_WS_URL : "ws://127.0.0.1:8000/ws/gate";
  if (gateNotifyWs && (gateNotifyWs.readyState === WebSocket.OPEN || gateNotifyWs.readyState === WebSocket.CONNECTING)) {
    return;
  }
  try {
    gateNotifyWs = new WebSocket(url);
    gateNotifyWs.onmessage = function (ev) {
      try {
        var msg = JSON.parse(ev.data);
        if (msg && msg.type === "GATE_CHANGED") {
          void pollGate({ force: true });
        }
      } catch (e) {
        /* ignore */
      }
    };
    gateNotifyWs.onclose = function () {
      gateNotifyWs = null;
      if (!studyHttpFallback) return;
      try {
        extAPI.alarms.create("gate-ws-retry", { delayInMinutes: 1 });
      } catch (e2) {
        /* ignore */
      }
    };
    gateNotifyWs.onerror = function () {
      try {
        gateNotifyWs.close();
      } catch (e3) {
        /* ignore */
      }
      gateNotifyWs = null;
    };
  } catch (e) {
    console.warn("CALT Gate: notify WS unavailable", e);
  }
}

async function softlandNativeGetDoc() {
  return nativeCall({ type: "get_softland_doc", schema_version: 1 }, 1200);
}

async function applyGatePayload(g, opts) {
  opts = opts || {};
  lastGateFetchAt = Date.now();
  noteGateFetchSuccess();
  var morning = g.morning || {};
  var browser = g.browser || {};
  gateCache = {
    ok: true,
    stale: false,
    degraded: false,
    source: g.source || opts.source || "unknown",
    fetched_at: lastGateFetchAt,
    policy_gen: g.policy_gen ?? null,
    locked: Boolean(g.locked),
    unlocked: Boolean(g.unlocked),
    enabled: Boolean(g.enabled),
    remaining_minutes: g.remaining_minutes ?? null,
    chapter_goal_met: Boolean(g.chapter_goal_met),
    day_unlimited: Boolean(g.day_unlimited),
    day_pass: Boolean(g.day_pass),
    reward_day: Boolean(g.reward_day),
    productive_minutes: g.productive_minutes ?? 0,
    daily_goal_minutes: g.daily_goal_minutes ?? 0,
    suggested_links: Array.isArray(g.suggested_links) ? g.suggested_links : [],
    current_block: g.current_block || null,
    morning: {
      next: morning.next || "open",
      bible_done: Boolean(morning.bible_done),
      plan_done: Boolean(morning.plan_done),
      bible_url: morning.bible_url || null,
      plan_url: morning.plan_url || null,
      redirect_url: morning.redirect_url || null,
      hint: morning.hint || "",
    },
    browser: browser,
    enforce: Boolean(browser.enforce) || Boolean(g.locked),
  };
  await extAPI.storage.local.set({ gateCache: gateCache });
  if (typeof applyContentScoreFromPolicy === "function") {
    applyContentScoreFromPolicy(browserPolicyOrFallback(browser));
  }
  var modeNow = String(browser.mode || "").toLowerCase();
  var freeNow =
    Boolean(g.day_unlimited) || Boolean(g.reward_day) || modeNow === "free";
  var modeKey =
    modeNow +
    "|" +
    (freeNow ? "1" : "0") +
    "|" +
    Boolean(browser.block_watch_sites) +
    "|" +
    Boolean(g.locked);
  var forceDnr = modeKey !== lastGateModeKey;
  if (forceDnr) lastGateModeKey = modeKey;
  await syncDeclarativeWatchBlock({ force: forceDnr || opts.force });
  await syncDeclarativePornBlock({ force: forceDnr || opts.force });
  scheduleGatePollAlarm();
}

async function pollGate(opts) {
  opts = opts || {};
  var now = Date.now();
  if (!opts.force && lastGateFetchAt && now - lastGateFetchAt < desiredGatePollMs() - 500) return;
  void syncQuinnFromFocus({ force: Boolean(opts.force) });
  try {
    var native = await softlandNativeGetDoc();
    if (native && native.ok !== false && (native.browser || native.enabled !== undefined)) {
      await applyGatePayload(native, { force: opts.force, source: "native" });
      return;
    }

    // Focus product lock: Study :8000 gate HTTP stays off.
    studyHttpFallback = false;
    if (!studyHttpFallback) {
      throw new Error(
        native && native.error
          ? "native:" + native.error
          : "native_softland_doc_unavailable"
      );
    }

    var r = await fetch(GATE_API_URL, { cache: "no-store", headers: caltExtensionHeaders() });
    if (!r.ok) throw new Error("HTTP " + r.status);
    var g = await r.json();
    g.source = "http";
    await applyGatePayload(g, { force: opts.force, source: "http" });
  } catch (e) {
    noteGateFetchFailure();
    scheduleGatePollAlarm();
    var err = String(e && e.message ? e.message : e);
    var prev = gateCache && typeof gateCache === "object" ? gateCache : null;
    var prevBrowser = (prev && prev.browser) || {};
    var prevMode = String(prevBrowser.mode || "").toLowerCase();
    var prevFree =
      prevMode === "free" ||
      Boolean(prev && (prev.day_unlimited || prev.reward_day || prev.unlocked));
    if (prev && (prev.ok || prev.degraded)) {
      gateCache = Object.assign({}, prev, {
        ok: true,
        stale: true,
        degraded: true,
        fetched_at: Date.now(),
        error: err,
        day_unlimited: Boolean(prev.day_unlimited) || prevFree,
        reward_day: Boolean(prev.reward_day),
        unlocked: Boolean(prev.unlocked) || prevFree,
        browser: Object.assign({}, prevBrowser, {
          block_watch_sites: prevFree ? false : true,
          block_social: prevFree ? false : prevBrowser.block_social === true,
          block_other: prevFree ? false : true,
          block_porn: prevBrowser.block_porn !== false,
          mode: prevFree ? "free" : prevMode || "study",
          mode_label: prevFree ? "FREE" : prevBrowser.mode_label || "STUDY",
          enforce: true,
        }),
      });
    } else {
      gateCache = {
        ok: true,
        stale: true,
        degraded: true,
        fetched_at: Date.now(),
        locked: true,
        unlocked: false,
        enabled: true,
        enforce: true,
        error: err,
        morning: { next: "open", bible_done: false, plan_done: false },
        browser: {
          mode: "study",
          mode_label: "STUDY",
          enforce: true,
          block_watch_sites: true,
          block_social: true,
          block_other: true,
          block_porn: true,
          block_keywords: true,
        },
      };
    }
    await extAPI.storage.local.set({ gateCache: gateCache });
    await syncDeclarativeWatchBlock({ force: true });
    await syncDeclarativePornBlock({ force: true });
  }
}

function desiredGatePollMs() {
  var s = typeof GATE_POLL_ACTIVE_S === "number" ? GATE_POLL_ACTIVE_S : 300;
  try {
    var iv = gateCache && gateCache.browser && gateCache.browser.intervals;
    if (iv && Number(iv.extension_gate_poll_s) > 0) {
      s = Number(iv.extension_gate_poll_s);
    }
  } catch (e) {
    /* ignore */
  }
  var base = Math.max(4000, Math.min(600000, Math.round(s * 1000)));
  if (gateFetchBackoffMs > base) return gateFetchBackoffMs;
  return base;
}

/** MV3-friendly: chrome.alarms only (no setInterval dual-poll). */
function scheduleGatePollAlarm() {
  var ms = desiredGatePollMs();
  GATE_POLL_MS = ms;
  var mins = Math.max(1, Math.min(10, Math.round(ms / 60000) || 5));
  try {
    extAPI.alarms.create("gate-poll", { periodInMinutes: mins });
  } catch (e) {
    /* ignore */
  }
}

function bootstrapGate() {
  if (gateBootstrapped) {
    scheduleGatePollAlarm();
    return;
  }
  gateBootstrapped = true;
  scheduleGatePollAlarm();
  void softlandNativePing();
  void pollGate({ force: true });
  if (studyHttpFallback) connectGateNotifyWs();
}

extAPI.alarms.onAlarm.addListener(function (alarm) {
  if (alarm.name === "gate-poll") void pollGate({ force: true });
  if (alarm.name === "gate-ws-retry" && studyHttpFallback) connectGateNotifyWs();
});

extAPI.runtime.onInstalled.addListener(function () {
  bootstrapGate();
});
extAPI.runtime.onStartup.addListener(function () {
  bootstrapGate();
});

bootstrapGate();

async function softLandBlockedTab(tabId, spaUrl, meta) {
  meta = meta || {};
  var kindMeta = String(meta.kind || "").toLowerCase();
  var hardKind =
    kindMeta.indexOf("porn") >= 0 ||
    kindMeta.indexOf("morning") >= 0 ||
    kindMeta === "keyword" ||
    kindMeta === "content_score" ||
    kindMeta.indexOf("content_score") >= 0 ||
    (meta.fromUrl && typeof isHardPornUrl === "function" && isHardPornUrl(meta.fromUrl)) ||
    (meta.host && typeof isHardPornUrl === "function" && isHardPornUrl("https://" + meta.host + "/")) ||
    (meta.fromUrl && typeof urlLooksAdult === "function" && urlLooksAdult(meta.fromUrl, ""));
  if (tabId == null) return false;
  if ((!redirectsEnabled && !hardKind) || (redirectsPausedByCircuit() && !hardKind)) return false;
  var mode = "";
  var next = "";
  try {
    mode = String((gateCache && gateCache.browser && gateCache.browser.mode) || "").toLowerCase();
    next = String((gateCache && gateCache.morning && gateCache.morning.next) || "").toLowerCase();
  } catch (e) {
    /* ignore */
  }
  var morningSoft =
    next === "bible" || next === "plan" || mode === "bible" || mode === "planning";
  var target = lockedPageUrl();
  if (morningSoft && spaUrl && String(spaUrl).indexOf("locked.html") < 0) {
    target = spaUrl;
  } else if (spaUrl && String(spaUrl).indexOf("locked.html") >= 0) {
    target = spaUrl;
  }
  if (softLandInFlight[tabId] === target) return false;
  var done = softLandDone[tabId];
  if (done && done.target === target && Date.now() - done.at < SOFTLAND_DEDUP_MS) return false;
  try {
    var existing = await extAPI.tabs.get(tabId);
    if (!existing) return false;
    var eu = existing.url ? String(existing.url) : "";
    if (tabUrlIsLockedPage(eu)) {
      // Already locked — treat as success so callers do NOT force-navigate again
      // (different ?why= / ?from= was causing the lock-page flicker loop).
      if (!morningSoft || tabUrlIsLockedPage(target)) {
        softLandDone[tabId] = { target: eu, at: Date.now() };
        return true;
      }
    }
    if (eu && typeof isCaltSpaUrl === "function" && isCaltSpaUrl(eu) && morningSoft) {
      softLandDone[tabId] = { target: target, at: Date.now() };
      return false;
    }
    // Circuit breaker must not swallow porn / content_score / morning locks.
    if (!hardKind && noteSoftLandAttempt()) return false;
    if (hardKind) noteSoftLandAttempt();
    softLandInFlight[tabId] = target;
    var blockedUrl = meta.fromUrl || eu || "";
    logExtensionEvent("soft_land", {
      kind: meta.kind || "blocked",
      detail: blockedUrl.slice(0, 200),
      url: blockedUrl.slice(0, 400),
      target: target.slice(0, 400),
      tab_id: tabId,
      notify: true,
    });
    var why = meta.kind ? String(meta.kind) : "blocked";
    var site = meta.host || blockedUrl.slice(0, 80) || "site";
    showBlockNotification(
      "CALT blocked a webpage",
      site + " · " + why + " — SoftLand redirected this tab (browser stayed open)."
    );
    await extAPI.tabs.update(tabId, { url: target });
    softLandDone[tabId] = { target: target, at: Date.now() };
    return true;
  } catch (e) {
    return false;
  } finally {
    delete softLandInFlight[tabId];
  }
}

async function softlandNativeGetMode(url) {
  var key = getModeCacheKey(url);
  var hit = getModeCache[key];
  var now = Date.now();
  if (hit && now - hit.at < GET_MODE_TTL_MS) return hit.msg;
  var msg = await nativeCall(
    {
      type: "get_mode",
      schema_version: 1,
      url: String(url || ""),
      tab_id: null,
      now: null,
    },
    900
  );
  if (msg && typeof msg === "object") {
    getModeCache[key] = { at: now, msg: msg };
  }
  return msg;
}

function inspectAllowsSoftlandKind(host, reason) {
  if (!inspectModeCached) return true;
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var r = String(reason || "").toLowerCase();
  // Morning gates + porn always enforce — Inspect must not open adult sites.
  if (r.indexOf("morning") >= 0 || r === "bible" || r === "plan") return true;
  if (r === "porn" || r === "keyword" || r.indexOf("porn") >= 0) return true;
  if (host && typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, pol)) return true;
  if (host && typeof isForcePornHost === "function" && isForcePornHost(host, pol)) return true;
  // Inspect: keep non-porn pages (YouTube etc.) so content-score overlays can paint.
  return false;
}

function isHardPornUrl(url) {
  var host = "";
  try {
    host = typeof hostnameFromUrl === "function" ? hostnameFromUrl(url) : "";
  } catch (e) {
    host = "";
  }
  if (!host) return false;
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  if (typeof isForcePornHost === "function" && isForcePornHost(host, pol)) return true;
  if (typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, pol)) return true;
  return false;
}

async function maybeRedirectTab(tabId, url, title) {
  // Must run before Gate v2 — webNavigation also fires for locked.html itself.
  // Re-scoring the interstitial + force tabs.update caused the lock-page flicker.
  if (!url) return false;
  if (tabUrlIsLockedPage(url)) return false;
  if (typeof isExtensionOrInternalUrl === "function" && isExtensionOrInternalUrl(url)) return false;
  if (typeof isCaltSpaUrl === "function" && isCaltSpaUrl(url)) return false;

  // Gate v2 scored pipeline (strict + grace). When it returns LOCK, redirect.
  if (typeof CaltGateV2 !== "undefined" && CaltGateV2.GATE_V2 && CaltGateV2.GATE_V2.enabled) {
    try {
      var modeV2 = String((gateCache && gateCache.browser && gateCache.browser.mode) || "free");
      var softOn =
        !(gateCache && gateCache.browser && gateCache.browser.softland_enabled === false);
      var v2 = CaltGateV2.onNav(tabId, url, title || "", {
        mode: modeV2,
        softlandEnabled: softOn,
        nativeReason: "",
      });
      if (v2 && typeof v2.then === "function") v2 = await v2;
      if (v2 && v2.verdict === "LOCK" && !v2.shadow) {
        var kindV2 = v2.hard || "blocked";
        var spaV2 = CaltGateV2.lockedUrl(
          extAPI.runtime.getURL(""),
          url,
          v2.reasons || [],
          kindV2,
        );
        reportGateAlert(kindV2, url.slice(0, 120));
        var lockedV2 = await softLandBlockedTab(tabId, spaV2, {
          fromUrl: url,
          kind: kindV2,
          host: v2.host || "",
        });
        if (!lockedV2) await forceNavigateToLock(tabId, spaV2);
        return true;
      }
      // WATCH / ALLOW — continue to native SoftLand for morning/watch; v2 already messaged blur.
    } catch (eGateV2) {
      console.warn("CALT Gate v2 nav", eGateV2);
    }
  }

  var host = "";
  try {
    host = typeof hostnameFromUrl === "function" ? hostnameFromUrl(url) : "";
  } catch (e) {
    host = "";
  }
  var polDirect =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var sensitiveDirect =
    host &&
    polDirect.block_sensitive !== false &&
    typeof isSensitiveHost === "function" &&
    isSensitiveHost(host, polDirect);
  // Watch redirects can pause. Porn and sensitive hosts (Grok, Reddit, Venice) still redirect.
  var hardPorn = isHardPornUrl(url);
  if (!redirectsEnabled && !hardPorn && !sensitiveDirect) return false;

  if (sensitiveDirect) {
    reportGateAlert("sensitive", url.slice(0, 120));
    var spaSens = lockedPageUrlForBlocked(url, "sensitive");
    var lockedSens = await softLandBlockedTab(tabId, spaSens, {
      fromUrl: url,
      kind: "sensitive",
      host: host,
    });
    if (!lockedSens) await forceNavigateToLock(tabId, spaSens);
    return true;
  }
  // Multi-stage adult path:
  // 1) known porn / sensitive hosts → lock now
  // 2) allow-listed + search hosts → never instant-lock from URL tokens; page must load
  // 3) content_score text → Quinn sentence confirm → optional screenshot
  // 4) appeal / temp-allow beat URL keyword locks (not hard-deny tubes)
  var allowResearchHost =
    host && typeof isFallbackAllowHost === "function" && isFallbackAllowHost(host) && !hardPorn;
  var searchPage = typeof webSearchQuery === "function" && webSearchQuery(url) !== null;
  var adultUrl = hardPorn;
  if (!adultUrl && !allowResearchHost && !searchPage) {
    if (typeof urlLooksAdult === "function") adultUrl = urlLooksAdult(url, title);
    else if (typeof urlLooksExplicit === "function") adultUrl = urlLooksExplicit(url, title);
  }

  // Quinn appeal grant (false-positive valve) — not for force/hard-deny tubes.
  var polAppeal =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  var hardDenyHost =
    host &&
    typeof isAppealHardDenyHost === "function" &&
    isAppealHardDenyHost(host, polAppeal);
  if (
    host &&
    !hardDenyHost &&
    typeof isHostAppealAllowed === "function" &&
    isHostAppealAllowed(host, appealAllowsCache, Date.now())
  ) {
    return false;
  }
  if (host && hostCoveredByActiveDnr(host)) {
    return false;
  }

  // Temp allow beats URL keyword locks on non-hard-deny hosts (60s reopen path).
  if (host && !hardDenyHost && typeof isHostTempAllowed === "function") {
    var pol =
      typeof browserPolicyOrFallback === "function"
        ? browserPolicyOrFallback(gateCache && gateCache.browser)
        : {};
    if (isHostTempAllowed(host, tempAllowsCache, Date.now(), pol)) {
      return false;
    }
  }

  // Unknown mirror cheat: adult words in path/query/title even if host is not seeded.
  if (adultUrl) {
    reportGateAlert("porn", url.slice(0, 120));
    var spaUrlAdult = lockedPageUrlForBlocked(url, "porn");
    var lockedAdult = await softLandBlockedTab(tabId, spaUrlAdult, {
      fromUrl: url,
      kind: "porn",
      host: host || "",
    });
    if (!lockedAdult) await forceNavigateToLock(tabId, spaUrlAdult);
    return true;
  }

  // Native SoftLand only — if msg_host fails, no SoftLand redirect (DNR still covers lists).
  var native = await softlandNativeGetMode(url);
  if (native && native.ok !== false && native.action) {
    if (native.action === "allow" || native.action === "none") return false;
    if (native.action === "block" && native.enforce !== false) {
      var kindN = native.reason || "blocked";
      if (!inspectAllowsSoftlandKind(host, kindN)) return false;
      reportGateAlert(kindN, url.slice(0, 120));
      var spaN = lockedPageUrlForBlocked(url, kindN, native.until || "");
      return softLandBlockedTab(tabId, spaN, {
        fromUrl: url,
        kind: kindN,
        host: host || native.matched || "",
        until: native.until || "",
      });
    }
  }
  return false;
}

if (extAPI.webNavigation && extAPI.webNavigation.onCommitted) {
  extAPI.webNavigation.onCommitted.addListener(function (details) {
    if (details.frameId !== 0) return;
    if (!details.url || !details.tabId) return;
    void maybeRedirectTab(details.tabId, details.url, "");
  });
}
if (extAPI.webNavigation && extAPI.webNavigation.onHistoryStateUpdated) {
  extAPI.webNavigation.onHistoryStateUpdated.addListener(function (details) {
    if (details.frameId !== 0) return;
    if (!details.url || !details.tabId) return;
    void maybeRedirectTab(details.tabId, details.url, "");
  });
}
if (extAPI.webNavigation && extAPI.webNavigation.onBeforeNavigate) {
  extAPI.webNavigation.onBeforeNavigate.addListener(function (details) {
    if (details.frameId !== 0) return;
    if (!details.url || !details.tabId) return;
    void maybeRedirectTab(details.tabId, details.url, "");
  });
}

if (extAPI.tabs && extAPI.tabs.onRemoved) {
  extAPI.tabs.onRemoved.addListener(function (tabId) {
    delete softLandDone[tabId];
    delete softLandInFlight[tabId];
  });
}

function pruneAndLoadTempAllows(cb) {
  extAPI.storage.local.get([TEMP_ALLOW_STORAGE_KEY || "tempAllows"], function (res) {
    var list = Array.isArray(res.tempAllows) ? res.tempAllows : [];
    list = typeof pruneTempAllows === "function" ? pruneTempAllows(list, Date.now()) : list;
    cb(list);
  });
}

extAPI.runtime.onMessage.addListener(function (msg, sender, sendResponse) {
  if (!msg || !msg.type) return false;

  // Gate v2 messages (appeal, classify, why panel, image signals)
  if (typeof CaltGateV2 !== "undefined" && CaltGateV2.handleMessage) {
    if (
      String(msg.type).indexOf("GATE_V2_") === 0 ||
      msg.type === "PORN_APPEAL_REQUEST" ||
      msg.type === "OFFSCREEN_NSFW_CLASSIFY"
    ) {
      if (msg.type === "PORN_APPEAL_REQUEST" && CaltGateV2.GATE_V2 && CaltGateV2.GATE_V2.enabled) {
        var handled = CaltGateV2.handleMessage(msg, sender, sendResponse);
        if (handled) return true;
      } else if (msg.type !== "PORN_APPEAL_REQUEST") {
        var handledV2 = CaltGateV2.handleMessage(msg, sender, sendResponse);
        if (handledV2) return true;
      }
    }
  }
  if (msg.type === "GATE_V2_FETCH_IMAGE") {
    void (async function () {
      try {
        var res = await fetch(String(msg.url || ""), {
          credentials: "omit",
          signal: AbortSignal.timeout(8000),
        });
        if (!res.ok) {
          sendResponse({ ok: false, error: "http_" + res.status });
          return;
        }
        var buf = await res.arrayBuffer();
        if (buf.byteLength > 4 * 1024 * 1024) {
          sendResponse({ ok: false, error: "too_large" });
          return;
        }
        var bytes = new Uint8Array(buf);
        var bin = "";
        for (var i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
        var b64 = btoa(bin);
        var ct = String(res.headers.get("content-type") || "image/jpeg").split(";")[0];
        sendResponse({ ok: true, dataUrl: "data:" + ct + ";base64," + b64 });
      } catch (eFetch) {
        sendResponse({ ok: false, scannerOffline: true, error: String(eFetch && eFetch.message) });
      }
    })();
    return true;
  }
  if (
    msg.type === "TEMP_ALLOW_REQUEST" &&
    typeof CaltGateV2 !== "undefined" &&
    CaltGateV2.GATE_V2 &&
    CaltGateV2.GATE_V2.enabled
  ) {
    sendResponse({
      ok: false,
      error: "Temp allow removed in Gate v2 — use Request review (cooldown).",
    });
    return true;
  }

  if (msg.type === "CONTENT_SCORE_WARN") {
    return false;
  }
  if (msg.type === "CONTENT_SCORE_LOCK") {
    var lockUrl = String(msg.url || "");
    var tabId = sender && sender.tab && sender.tab.id;
    var excerptCs = String(msg.excerpt || "").slice(0, 1800);
    var matchedCs = Array.isArray(msg.matched) ? msg.matched.slice(0, 12) : [];
    var titleCs = String(msg.title || "").slice(0, 200);
    var scoreCs = Number(msg.score) || 0;
    void (async function () {
      var hostPeek = "";
      try {
        hostPeek = typeof hostnameFromUrl === "function" ? hostnameFromUrl(lockUrl) : "";
      } catch (ePeek) {
        hostPeek = "";
      }
      var polPeek =
        typeof browserPolicyOrFallback === "function"
          ? browserPolicyOrFallback(gateCache && gateCache.browser)
          : {};
      var modeCs = "free";
      try {
        var mCs = String((gateCache && gateCache.browser && gateCache.browser.mode) || "").toLowerCase();
        modeCs = mCs === "study" || mCs === "bible" || mCs === "planning" ? "study" : "free";
      } catch (eMode) {
        modeCs = "free";
      }
      var thCs =
        typeof CONTENT_SCORE_THRESHOLDS !== "undefined" && CONTENT_SCORE_THRESHOLDS[modeCs]
          ? CONTENT_SCORE_THRESHOLDS[modeCs]
          : { warn: 8, lock: 10 };
      var lockFloor = Number(thCs.lock) || 10;
      var warnFloor = Number(thCs.warn) || 8;

      // Unknown mirrors: do not depend on SoftLand host seeds.
      // Allow-listed research hosts ignore the bare word "porn" in the URL.
      var researchPeek =
        hostPeek && typeof isFallbackAllowHost === "function" && isFallbackAllowHost(hostPeek);
      var searchPagePeek =
        typeof webSearchQuery === "function" && webSearchQuery(lockUrl) !== null;
      // Search / allow-list: URL tokens alone are not a hard lock — text + Quinn + image decide.
      var listHard =
        (!(researchPeek || searchPagePeek) &&
          ((typeof urlLooksAdult === "function" && urlLooksAdult(lockUrl, titleCs)) ||
            (typeof urlLooksExplicit === "function" && urlLooksExplicit(lockUrl, titleCs)))) ||
        (hostPeek &&
          typeof isAppealHardDenyHost === "function" &&
          isAppealHardDenyHost(hostPeek, polPeek)) ||
        (hostPeek && typeof isForcePornHost === "function" && isForcePornHost(hostPeek, polPeek));
      var scoreHard = scoreCs >= 20;

      // Quinn false-positive grant — pause content-score lock for the appeal window.
      if (
        hostPeek &&
        typeof isHostAppealAllowed === "function" &&
        typeof isAppealHardDenyHost === "function" &&
        !isAppealHardDenyHost(hostPeek, polPeek) &&
        isHostAppealAllowed(hostPeek, appealAllowsCache, Date.now())
      ) {
        sendResponse({ ok: true, skipped: true, reason: "appeal_allow" });
        return;
      }

      // SoftLand allow-list may open the host — keyword score still overrides.
      var native = null;
      try {
        native = await softlandNativeGetMode(lockUrl);
      } catch (eNat) {
        native = null;
      }
      var overAllow = Boolean(
        native &&
          (native.reason === "allow_list" ||
            (native.action === "allow" && String(native.reason || "").indexOf("allow") >= 0))
      );
      // Allow-listed hosts: lock from warn floor (keywords checked even after Allow site).
      var needScore = overAllow ? warnFloor : lockFloor;
      var adultLock = listHard || scoreHard || scoreCs >= needScore;

      if (!redirectsEnabled && !adultLock) {
        sendResponse({ ok: false, reason: "redirects_off" });
        return;
      }
      // Inspect: only soft (non-adult) content-score locks stay paused.
      if (inspectModeCached && !adultLock) {
        sendResponse({ ok: true, skipped: true, reason: "inspect" });
        return;
      }

      // Score below floor and not list-hard → do not lock.
      if (scoreCs < needScore && !listHard && !scoreHard) {
        sendResponse({ ok: true, skipped: true, reason: "below_lock", need: needScore });
        return;
      }
      var allowSoft =
        typeof allowHostContentLock === "function"
          ? allowHostContentLock({
              host: hostPeek,
              url: lockUrl,
              title: titleCs,
              excerpt: String(msg.excerpt || ""),
              matched: matchedCs,
              score: scoreCs,
            })
          : { skip: false };
      if (allowSoft.skip) {
        sendResponse({ ok: true, skipped: true, reason: allowSoft.why || "allow_host_soft" });
        return;
      }

      var searchPageCs =
        typeof webSearchQuery === "function" && webSearchQuery(lockUrl) !== null;
      var quinnSkip =
        (hostPeek &&
          typeof isAppealHardDenyHost === "function" &&
          isAppealHardDenyHost(hostPeek, polPeek)) ||
        (hostPeek && typeof isSensitiveHost === "function" && isSensitiveHost(hostPeek, polPeek)) ||
        (!searchPageCs &&
          typeof urlLooksExplicit === "function" &&
          urlLooksExplicit(lockUrl, titleCs));
      if (!quinnSkip && typeof askQuinnSentenceConfirm === "function") {
        var quinnVerdict = null;
        try {
          quinnVerdict = await askQuinnSentenceConfirm({
            host: hostPeek,
            url: lockUrl,
            title: titleCs,
            excerpt: String(msg.excerpt || ""),
            matched: matchedCs,
            score: scoreCs,
          });
        } catch (eQuinn) {
          quinnVerdict = null;
        }
        if (quinnVerdict && quinnVerdict.allow) {
          sendResponse({
            ok: true,
            skipped: true,
            reason: "quinn_finalize",
            why: quinnVerdict.why || "",
          });
          return;
        }
      }

      // Last stage: screenshot. A clean image cancels the block on search
      // and allow-listed hosts. Known tubes never reach this.
      var imageHost =
        hostPeek &&
        !(typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(hostPeek, polPeek)) &&
        !(typeof isForcePornHost === "function" && isForcePornHost(hostPeek, polPeek)) &&
        (searchPageCs ||
          (typeof isFallbackAllowHost === "function" && isFallbackAllowHost(hostPeek)));
      if (imageHost && typeof tabId === "number" && typeof askImageVerify === "function") {
        var seen = null;
        try {
          seen = await askImageVerify(tabId);
        } catch (eImg) {
          seen = null;
        }
        if (seen && seen.adult === false) {
          sendResponse({
            ok: true,
            skipped: true,
            reason: "image_clean",
            why: seen.why || "",
          });
          return;
        }
      }

      // Keyword locks stay "content_score" / "keyword" — not "porn" — unless host is a hard tube.
      var kindCs = "keyword";
      if (listHard) kindCs = "porn";
      else if (scoreHard || scoreCs >= lockFloor) kindCs = "keyword";
      reportGateAlert(
        kindCs === "porn" ? "porn" : "distraction",
        "content_score:" +
          scoreCs +
          (overAllow ? " over_allow_list" : "") +
          " " +
          lockUrl.slice(0, 100)
      );
      if (typeof tabId !== "number") {
        sendResponse({ ok: false, reason: "no_tab" });
        return;
      }
      var target = lockedPageUrlForBlocked(lockUrl, kindCs);
      var locked = await softLandBlockedTab(tabId, target, {
        fromUrl: lockUrl,
        kind: kindCs,
        host: hostPeek || (native && native.matched) || "",
      });
      if (!locked) locked = await forceNavigateToLock(tabId, target);
      sendResponse({
        ok: true,
        locked: Boolean(locked),
        score: scoreCs,
        over_allow_list: overAllow,
      });
    })();
    return true;
  }
  if (msg.type === "GET_GATE") {
    void syncQuinnFromFocus({ force: true }).then(function () {
      extAPI.storage.local.get(["caltInspectMode"], function (st) {
        sendResponse({
          gateCache: gateCache,
          redirectsEnabled: redirectsEnabled,
          nativeHostOk: nativeHostOk,
          studyHttpFallback: studyHttpFallback,
          inspectMode: Boolean(st && st.caltInspectMode),
          quinnEnabled: quinnEnabledCached !== false,
        });
      });
    });
    return true;
  }
  // Legacy: Quinn on/off is owned by Focus Home — refresh from mirror only.
  if (msg.type === "SET_QUINN_ENABLED") {
    void syncQuinnFromFocus({ force: true }).then(function () {
      sendResponse({ ok: true, quinnEnabled: quinnEnabledCached !== false, source: "focus" });
    });
    return true;
  }
  if (msg.type === "SET_INSPECT_MODE") {
    var onBg = Boolean(msg.enabled);
    inspectModeCached = onBg;
    extAPI.storage.local.set({ caltInspectMode: onBg }, function () {
      void syncDeclarativeWatchBlock({ force: true });
      void syncDeclarativePornBlock({ force: true });
      sendResponse({ ok: true, inspectMode: onBg });
    });
    return true;
  }
  if (msg.type === "INSPECT_GET_MODE") {
    void softlandNativeGetMode(String(msg.url || "")).then(function (native) {
      sendResponse({ ok: true, native: native || null, nativeHostOk: nativeHostOk });
    });
    return true;
  }
  if (msg.type === "REFRESH_GATE") {
    void pollGate({ force: true }).then(function () {
      return syncQuinnFromFocus({ force: true });
    }).then(function () {
      extAPI.storage.local.get(["caltInspectMode"], function (st) {
        sendResponse({
          ok: true,
          gateCache: gateCache,
          nativeHostOk: nativeHostOk,
          studyHttpFallback: studyHttpFallback,
          inspectMode: Boolean(st && st.caltInspectMode),
          quinnEnabled: quinnEnabledCached !== false,
        });
      });
    });
    return true;
  }
  if (msg.type === "SET_REDIRECTS") {
    redirectsEnabled = true;
    extAPI.storage.local.set({ redirectsEnabled: true });
    void syncDeclarativeWatchBlock({ force: true });
    void syncDeclarativePornBlock({ force: true });
    sendResponse({ ok: true, redirectsEnabled: true });
    return false;
  }
  if (msg.type === "TEMP_ALLOW_STATUS") {
    var hostS = String(msg.host || "")
      .toLowerCase()
      .replace(/^www\./, "");
    pruneAndLoadTempAllows(function (list) {
      var polS = typeof browserPolicyOrFallback === "function" ? browserPolicyOrFallback(gateCache && gateCache.browser) : {};
      var excludedS = !hostS || (typeof isTempAllowExcludedHost === "function" && isTempAllowExcludedHost(hostS, polS));
      var untilS =
        typeof tempAllowUntilForHost === "function" ? tempAllowUntilForHost(hostS, list, Date.now()) : 0;
      sendResponse({
        ok: true,
        host: hostS,
        excluded: excludedS,
        can_allow: Boolean(hostS) && !excludedS,
        allowed_until: untilS || 0,
        temp_allow_ms: typeof TEMP_ALLOW_MS !== "undefined" ? TEMP_ALLOW_MS : 60000,
        temp_allows: list,
      });
    });
    return true;
  }
  if (msg.type === "TEMP_ALLOW_REQUEST") {
    var hostR = String(msg.host || "")
      .toLowerCase()
      .replace(/^www\./, "");
    var nowR = Date.now();
    var polR = typeof browserPolicyOrFallback === "function" ? browserPolicyOrFallback(gateCache && gateCache.browser) : {};
    var grant = typeof buildTempAllowGrant === "function" ? buildTempAllowGrant(hostR, nowR, polR) : { ok: false };
    if (!grant.ok) {
      sendResponse({
        ok: false,
        error: (grant && grant.error) || "This site can't be temporarily allowed",
        host: hostR,
      });
      return false;
    }
    pruneAndLoadTempAllows(function (list) {
      list = typeof upsertTempAllow === "function" ? upsertTempAllow(list, grant.entry, nowR) : list.concat([grant.entry]);
      tempAllowsCache = list;
      var key = TEMP_ALLOW_STORAGE_KEY || "tempAllows";
      var payload = {};
      payload[key] = list;
      payload.tempAllows = list;
      extAPI.storage.local.set(payload, function () {
        invalidateGetModeCache(grant.entry.host);
        sendResponse({
          ok: true,
          host: grant.entry.host,
          until: grant.entry.until,
          temp_allow_ms: typeof TEMP_ALLOW_MS !== "undefined" ? TEMP_ALLOW_MS : 60000,
        });
      });
    });
    return true;
  }
  if (msg.type === "PORN_APPEAL_REQUEST") {
    void handlePornAppealRequest(msg).then(function (resp) {
      sendResponse(resp);
    });
    return true;
  }
  return false;
});

/** Search-results URL → the query is the use case. Otherwise the case is the page itself. */
function quinnWebSearchUseCase(url) {
  var u;
  try {
    u = new URL(String(url || ""));
  } catch (e) {
    return { useCase: "page", query: "" };
  }
  var h = u.hostname.toLowerCase().replace(/^www\./, "");
  var q =
    u.searchParams.get("q") ||
    u.searchParams.get("query") ||
    u.searchParams.get("p") ||
    u.searchParams.get("text") ||
    "";
  var searchHost =
    /(^|\.)google\./.test(h) ||
    h === "bing.com" ||
    h.endsWith(".bing.com") ||
    h === "duckduckgo.com" ||
    h.endsWith(".duckduckgo.com") ||
    h === "search.brave.com" ||
    h === "ecosia.org" ||
    h === "startpage.com" ||
    h === "kagi.com" ||
    h === "search.yahoo.com";
  var searchPath = u.pathname.indexOf("/search") >= 0 || h.indexOf("duckduckgo.") >= 0;
  if (searchHost && (searchPath || q)) {
    return { useCase: "web_search", query: String(q).replace(/\s+/g, " ").trim().slice(0, 300) };
  }
  return { useCase: "page", query: "" };
}

/** Headless read: extension fetch, no tab. Title plus visible text. */
async function quinnHeadlessContent(url) {
  var empty = { title: "", text: "" };
  var raw = String(url || "");
  if (raw.indexOf("http://") !== 0 && raw.indexOf("https://") !== 0) return empty;
  if (raw.indexOf("locked.html") >= 0) return empty;
  try {
    var res = await fetch(raw, {
      method: "GET",
      redirect: "follow",
      credentials: "omit",
      signal: AbortSignal.timeout(4000),
    });
    if (!res.ok) return empty;
    var ct = String(res.headers.get("content-type") || "");
    if (ct && ct.indexOf("text") < 0 && ct.indexOf("html") < 0 && ct.indexOf("json") < 0) return empty;
    var html = await res.text();
    if (!html) return empty;
    html = html.replace(/<script[\s\S]*?<\/script>/gi, " ").replace(/<style[\s\S]*?<\/style>/gi, " ");
    var title = "";
    var tm = html.match(/<title[^>]*>([\s\S]*?)<\/title>/i);
    if (tm) title = tm[1].replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim().slice(0, 200);
    var text = html
      .replace(/<[^>]+>/g, " ")
      .replace(/&nbsp;/gi, " ")
      .replace(/&amp;/gi, "&")
      .replace(/\s+/g, " ")
      .trim()
      .slice(0, 1400);
    return { title: title, text: text };
  } catch (e) {
    return empty;
  }
}

function quinnPlain(s) {
  return String(s || "")
    .replace(/<[^>]+>/g, " ")
    .replace(/&amp;/gi, "&")
    .replace(/&quot;/gi, '"')
    .replace(/&#39;|&apos;/gi, "'")
    .replace(/&lt;/gi, "<")
    .replace(/&gt;/gi, ">")
    .replace(/&nbsp;/gi, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function quinnParseSearchHits(html) {
  var src = String(html || "");
  var titles = [];
  var snippets = [];
  var m;
  var reT = /class="result__a"[^>]*>([\s\S]*?)<\/a>/gi;
  while ((m = reT.exec(src)) && titles.length < 4) {
    var t = quinnPlain(m[1]).slice(0, 140);
    if (t) titles.push(t);
  }
  var reS = /class="result__snippet"[^>]*>([\s\S]*?)<\/a>/gi;
  while ((m = reS.exec(src)) && snippets.length < 4) {
    var sn = quinnPlain(m[1]).slice(0, 220);
    if (sn) snippets.push(sn);
  }
  var out = [];
  var n = Math.max(titles.length, snippets.length);
  for (var i = 0; i < n && out.length < 4; i++) {
    var line = titles[i] || "";
    if (snippets[i]) line = line ? line + " — " + snippets[i] : snippets[i];
    if (line) out.push("- " + line);
  }
  return out.join("\n").slice(0, 900);
}

/** Headless web search: what this website is used for. No tab. */
async function quinnWebSearch(query) {
  var q = String(query || "")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 180);
  if (!q) return "";
  var signal = AbortSignal.timeout(4500);
  var abstractText = "";
  var hits = "";
  try {
    var pair = await Promise.all([
      fetch(
        "https://api.duckduckgo.com/?format=json&no_html=1&skip_disambig=1&q=" + encodeURIComponent(q),
        { credentials: "omit", signal: signal }
      )
        .then(function (r) {
          return r.ok ? r.json() : null;
        })
        .catch(function () {
          return null;
        }),
      fetch("https://html.duckduckgo.com/html/?q=" + encodeURIComponent(q), {
        credentials: "omit",
        signal: signal,
        headers: { Accept: "text/html" },
      })
        .then(function (r) {
          return r.ok ? r.text() : "";
        })
        .catch(function () {
          return "";
        }),
    ]);
    var j = pair[0];
    if (j && j.AbstractText) {
      abstractText = quinnPlain((j.Heading ? j.Heading + ": " : "") + j.AbstractText).slice(0, 500);
    }
    hits = quinnParseSearchHits(pair[1] || "");
  } catch (e) {
    return "";
  }
  if (abstractText && hits) return (abstractText + "\n" + hits).slice(0, 1100);
  return abstractText || hits || "";
}

/**
 * Evidence Quinn must read before it finalizes:
 * page context, a web search of what the website is used for, and the site name.
 */
async function gatherQuinnCase(payload) {
  payload = payload || {};
  var host = typeof normalizeHost === "function" ? normalizeHost(payload.host || "") : String(payload.host || "");
  var url = String(payload.url || "");
  var title = String(payload.title || "").slice(0, 200);
  var content = String(payload.excerpt || payload.content || "").slice(0, 1400);
  var search = quinnWebSearchUseCase(url);
  var websiteUsedFor = "";
  if (search.useCase === "web_search") {
    try {
      var qu = new URL(url);
      ["gs_lcrp", "cvid", "form", "pglt", "pc", "adppc", "qs", "sp", "pq", "sc", "sk"].forEach(
        function (k) {
          qu.searchParams.delete(k);
          qu.searchParams.delete(k.toUpperCase());
        }
      );
      url = qu.toString();
    } catch (eClean) {
      /* keep original url */
    }
  }
  var pending = [];
  if (content.length < 80 && url) {
    pending.push(
      quinnHeadlessContent(url).then(function (fetched) {
        if (!title && fetched.title) title = fetched.title;
        if (fetched.text && fetched.text.length > content.length) content = fetched.text;
      })
    );
  }
  if (host) {
    pending.push(
      quinnWebSearch("what is " + host + " used for").then(function (found) {
        websiteUsedFor = found || "";
      })
    );
  }
  if (pending.length) await Promise.all(pending);
  return {
    websiteName: host || "(unknown)",
    url: url.slice(0, 400),
    title: title,
    content: content,
    useCase: search.useCase,
    query: search.query,
    note: String(payload.note || "").slice(0, 400),
    websiteUsedFor: websiteUsedFor,
  };
}

function quinnEvidenceBlock(c) {
  c = c || {};
  return (
    "website_name: " +
    (c.websiteName || "(unknown)") +
    "\nwebsite_used_for:\n" +
    (c.websiteUsedFor || "(no web search result)") +
    "\nuse_case: " +
    (c.useCase || "page") +
    "\nsearch_query: " +
    (c.query || "(none)") +
    "\npage_title: " +
    (c.title || "(none)") +
    "\nuser_note: " +
    (c.note || "(none)") +
    "\ncontent:\n" +
    (c.content || "(none)")
  );
}

var visionModelCached = "";

function ollamaOrigin() {
  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "")
    .replace(/\/$/, "");
  return base.replace(/\/v1$/, "");
}

/** Prefer a local vision model. The text Quinn model cannot see a screenshot. */
async function resolveVisionModel() {
  if (visionModelCached) return visionModelCached;
  if (llmHostPrefs && llmHostPrefs.visionModel) {
    visionModelCached = String(llmHostPrefs.visionModel);
    return visionModelCached;
  }
  try {
    var res = await fetch(ollamaOrigin() + "/api/tags", { signal: AbortSignal.timeout(2500) });
    if (!res.ok) return "";
    var j = await res.json();
    var models = (j && j.models) || [];
    var i;
    for (i = 0; i < models.length; i++) {
      var name = String((models[i] && (models[i].name || models[i].model)) || "");
      if (/vl|llava|moondream|minicpm-v|bakllava|vision/i.test(name)) {
        visionModelCached = name;
        return name;
      }
    }
  } catch (e) {
    return "";
  }
  return "";
}

function captureVisibleJpeg(tabId) {
  return new Promise(function (resolve) {
    if (!extAPI.tabs || !extAPI.tabs.get || !extAPI.tabs.captureVisibleTab) {
      resolve("");
      return;
    }
    extAPI.tabs.get(tabId, function (tab) {
      if (extAPI.runtime && extAPI.runtime.lastError) {
        resolve("");
        return;
      }
      if (!tab || !tab.active || tab.windowId == null) {
        resolve("");
        return;
      }
      var shot = extAPI.tabs.captureVisibleTab(tab.windowId, { format: "jpeg", quality: 28 });
      if (shot && typeof shot.then === "function") {
        shot.then(function (url) { resolve(url || ""); }).catch(function () { resolve(""); });
      } else {
        resolve("");
      }
    });
  });
}

/**
 * Last block stage. adult:false means the screenshot is not porn, so the
 * text score must not lock the tab. adult:null means no vision model or no shot.
 */
async function askImageVerify(tabId) {
  var dataUrl = await captureVisibleJpeg(tabId);
  if (!dataUrl || String(dataUrl).indexOf("data:image") !== 0) {
    return { adult: null, why: "no screenshot" };
  }
  var model = await resolveVisionModel();
  if (!model) return { adult: null, why: "no vision model" };
  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "")
    .replace(/\/$/, "");
  var endpoint = base + "/chat/completions";
  var headers = { "Content-Type": "application/json" };
  if (llmHostPrefs && llmHostPrefs.apiKey) headers.Authorization = "Bearer " + llmHostPrefs.apiKey;
  try {
    var res = await fetch(endpoint, {
      method: "POST",
      headers: headers,
      body: JSON.stringify({
        model: model,
        temperature: 0.05,
        messages: [
          {
            role: "system",
            content:
              "You are Quinn, the last stage before a CALT block. Look at the browser screenshot. " +
              "adult true only for porn, nudity, a sex act, a cam, or an explicit gallery. " +
              "A search page, news page, code, or docs page is adult false even if an adult word is visible. " +
              'Reply ONLY JSON: {"adult":true|false,"why":"what the image shows"}.',
          },
          {
            role: "user",
            content: [
              { type: "text", text: "Is this screenshot adult content that should be blocked?" },
              { type: "image_url", image_url: { url: dataUrl } },
            ],
          },
        ],
      }),
      signal: AbortSignal.timeout(20000),
    });
    if (!res.ok) return { adult: null, why: "vision HTTP " + res.status };
    var j = await res.json();
    var text = String((((j.choices || [])[0] || {}).message || {}).content || "").trim();
    var brace = text.indexOf("{");
    var end = text.lastIndexOf("}");
    if (brace >= 0 && end > brace) text = text.slice(brace, end + 1);
    var parsed = JSON.parse(text);
    if (typeof parsed.adult !== "boolean") return { adult: null, why: "vision parse" };
    return { adult: parsed.adult, why: String(parsed.why || "").slice(0, 200), model: model };
  } catch (e) {
    return { adult: null, why: "vision unreachable" };
  }
}

/**
 * Pre-lock sentence confirmation after keyword content_score.
 * Distinguishes "how to block adult content" chats from actual porn pages.
 * Fail-closed on errors / low confidence.
 */
async function askQuinnSentenceConfirm(payload) {
  var host = normalizeHost(payload.host || "");
  var url = String(payload.url || "");
  var title = String(payload.title || "").slice(0, 200);
  var excerpt = String(payload.excerpt || "").slice(0, 1400);
  var matched = Array.isArray(payload.matched) ? payload.matched.slice(0, 10) : [];
  var score = Number(payload.score) || 0;

  await syncQuinnFromFocus({ force: false });
  if (quinnEnabledCached === false) {
    return { allow: false, why: "Quinn OFF", error: "quinn_off" };
  }
  var polS =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};
  if (typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, polS)) {
    return { allow: false, why: "Known adult host", hard_deny: true, confidence: 1 };
  }
  if (typeof isSensitiveHost === "function" && isSensitiveHost(host, polS)) {
    return { allow: false, why: "Sensitive host", hard_deny: true, confidence: 1 };
  }

  var evidence = await gatherQuinnCase({
    host: host,
    url: url,
    title: title,
    excerpt: excerpt,
    note: "",
  });

  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "").replace(
    /\/$/,
    ""
  );
  var model = String((llmHostPrefs && llmHostPrefs.model) || QWEN_DEFAULT_MODEL || "qwen2.5:3b");
  var endpoint = base + "/chat/completions";

  var system =
    "You are Quinn, CALT sentence confirmer. Evidence is already collected headlessly. " +
    "Read website_name, website_used_for (a web search of what that site is for), content, and use_case, then finalize. " +
    "The why sentence must say what the website is used for. " +
    "Allow a code host, encyclopedia, docs site, news site, or a web search whose query is research or how to block adult content. " +
    "Block porn media, a tube, a cam site, or a search that asks for porn or hentai. " +
    'Reply ONLY JSON: {"allow":true|false,"confidence":0.0-1.0,"why":"what the site is for, then the decision"}. ' +
    "Default allow:false. Never allow tubes, hentai, cams, OnlyFans-style hosts, or erotic galleries.";

  var user =
    quinnEvidenceBlock(evidence) +
    "\nurl: " +
    url.slice(0, 400) +
    "\nkeyword_score: " +
    score +
    "\nmatched_keywords: " +
    matched.join(", ");

  try {
    var headers = { "Content-Type": "application/json" };
    if (llmHostPrefs && llmHostPrefs.apiKey) {
      headers.Authorization = "Bearer " + llmHostPrefs.apiKey;
    }
    var res = await fetch(endpoint, {
      method: "POST",
      headers: headers,
      body: JSON.stringify({
        model: model,
        temperature: 0.05,
        messages: [
          { role: "system", content: system },
          { role: "user", content: user },
        ],
      }),
      signal: AbortSignal.timeout(12000),
    });
    if (!res.ok) {
      return { allow: false, why: "Quinn HTTP " + res.status, error: "llm_http" };
    }
    var j = await res.json();
    var text = (((j.choices || [])[0] || {}).message || {}).content || "";
    text = String(text).trim();
    var jsonStr = text;
    var brace = text.indexOf("{");
    var end = text.lastIndexOf("}");
    if (brace >= 0 && end > brace) jsonStr = text.slice(brace, end + 1);
    var parsed = {};
    try {
      parsed = JSON.parse(jsonStr);
    } catch (eParse) {
      return { allow: false, why: "Quinn parse fail — lock", error: "llm_parse" };
    }
    var allow = Boolean(parsed.allow);
    var confidence = Number(parsed.confidence);
    if (!(confidence >= 0 && confidence <= 1)) confidence = allow ? 0.5 : 0.8;
    if (allow && confidence < 0.7) {
      return {
        allow: false,
        why: "Low sentence-confirm confidence — lock",
        confidence: confidence,
      };
    }
    return {
      allow: allow,
      why: String(parsed.why || (allow ? "Advice context." : "Adult context.")).slice(0, 240),
      confidence: confidence,
      model: model,
      source: "sentence_quinn",
    };
  } catch (e) {
    return {
      allow: false,
      why: "Quinn timeout/unreachable — lock",
      error: e && e.message ? e.message : "unreachable",
    };
  }
}

/**
 * Ask local Qwen (Ollama) whether a SoftLand porn / content_score block looks like a
 * false positive. Hard-deny known adult hosts without calling the model.
 */
async function askQuinnAppeal(payload) {
  var host = normalizeHost(payload.host || "");
  var url = String(payload.url || "");
  var reason = String(payload.reason || "");
  var note = String(payload.note || "").slice(0, 400);
  var title = String(payload.title || "").slice(0, 200);
  var excerpt = String(payload.excerpt || "").slice(0, 1200);
  var pol =
    typeof browserPolicyOrFallback === "function"
      ? browserPolicyOrFallback(gateCache && gateCache.browser)
      : {};

  if (!host) return { allow: false, why: "No host to review.", hard_deny: true };
  await syncQuinnFromFocus({ force: true });
  if (quinnEnabledCached === false) {
    return {
      allow: false,
      why: "Quinn is OFF - turn on in Focus Home (Quinn chip).",
      error: "quinn_off",
    };
  }
  if (typeof isAppealHardDenyHost === "function" && isAppealHardDenyHost(host, pol)) {
    return {
      allow: false,
      why: "Known adult / porn host — Quinn will not override this block.",
      hard_deny: true,
      confidence: 1,
    };
  }
  if (typeof isSensitiveHost === "function" && isSensitiveHost(host, pol)) {
    return {
      allow: false,
      why: "Sensitive host — Quinn will not open this.",
      hard_deny: true,
      confidence: 1,
    };
  }

  var researchHost =
    typeof isFallbackAllowHost === "function" &&
    isFallbackAllowHost(host) &&
    !(typeof isForcePornHost === "function" && isForcePornHost(host, pol));
  var searchQuery =
    typeof webSearchQuery === "function" ? webSearchQuery(url) : null;
  var searchPage = searchQuery !== null;
  var explicitAppeal =
    typeof urlLooksExplicit === "function" && urlLooksExplicit(url, title);
  var adultAppeal =
    typeof urlLooksAdult === "function" && urlLooksAdult(url, title);

  // Stage-0 appeal: allow-listed hosts (Claude, Bing, GitHub, …) are not tubes.
  // softland_reason=porn only means the auto-lock fired — often a false positive.
  if (researchHost && !explicitAppeal && !adultAppeal) {
    var openWhy = searchPage
      ? "Search engine — query is not adult (" +
        String(searchQuery || "").slice(0, 80) +
        "). Opened for this appeal."
      : "Allow-listed host (" +
        host +
        ") — not a tube. Opened for this appeal.";
    return {
      allow: true,
      why: openWhy,
      confidence: 0.96,
      source: searchPage ? "search_query_allow" : "allow_list_appeal",
    };
  }

  var evidence = await gatherQuinnCase({
    host: host,
    url: url,
    title: title,
    excerpt: excerpt,
    note: note,
  });

  var base = String((llmHostPrefs && llmHostPrefs.baseUrl) || QWEN_DEFAULT_BASE || "")
    .replace(/\/$/, "");
  var model = String((llmHostPrefs && llmHostPrefs.model) || QWEN_DEFAULT_MODEL || "qwen2.5:3b");
  var endpoint = base + "/chat/completions";

  var system =
    "You are Quinn, CALT Focus appeal reviewer. Evidence is already collected headlessly. " +
    "Read website_name, website_used_for, use_case, search_query, content, and the user note, then finalize. " +
    "The why sentence must say what the website is used for. " +
    "lock_trigger is ONLY why SoftLand auto-locked — it is often a false positive. " +
    "Never treat lock_trigger=porn or the word porn in a user note about blocking porn as proof the site is a tube. " +
    "Claude, ChatGPT, Gemini, GitHub, docs, and search engines are tools — allow:true unless the URL itself asks for porn. " +
    "If the user note says they are building a porn blocker or asking for help, allow:true. " +
    "Block only porn media, tubes, cams, or a search that asks for porn or hentai. " +
    "allow must match why. Reply with ONLY compact JSON: " +
    '{"allow":true|false,"confidence":0.0-1.0,"why":"what the site is for, then the decision"}. ' +
    "Known tubes never reach you.";

  var user =
    quinnEvidenceBlock(evidence) +
    "\nurl: " +
    evidence.url +
    "\nlock_trigger: " +
    reason +
    "\nlock_trigger_note: automatic SoftLand label — not ground truth that the site is porn";

  try {
    var headers = { "Content-Type": "application/json" };
    if (llmHostPrefs && llmHostPrefs.apiKey) {
      headers.Authorization = "Bearer " + llmHostPrefs.apiKey;
    }
    var res = await fetch(endpoint, {
      method: "POST",
      headers: headers,
      body: JSON.stringify({
        model: model,
        temperature: 0.1,
        messages: [
          { role: "system", content: system },
          { role: "user", content: user },
        ],
      }),
      signal: AbortSignal.timeout(35000),
    });
    if (!res.ok) {
      return { allow: false, why: "Quinn unreachable (HTTP " + res.status + ").", error: "llm_http" };
    }
    var j = await res.json();
    var text = (((j.choices || [])[0] || {}).message || {}).content || "";
    text = String(text).trim();
    var jsonStr = text;
    var brace = text.indexOf("{");
    var end = text.lastIndexOf("}");
    if (brace >= 0 && end > brace) jsonStr = text.slice(brace, end + 1);
    var parsed = {};
    try {
      parsed = JSON.parse(jsonStr);
    } catch (eParse) {
      return { allow: false, why: "Quinn reply was not valid JSON — keeping block.", error: "llm_parse", raw: text.slice(0, 200) };
    }
    var allow = Boolean(parsed.allow);
    var confidence = Number(parsed.confidence);
    if (!(confidence >= 0 && confidence <= 1)) confidence = allow ? 0.5 : 0.8;
    var whyText = String(parsed.why || "").slice(0, 240);
    // Require clear confidence to open a blocked site.
    if (allow && confidence < 0.65) {
      return {
        allow: false,
        why: (whyText || "Low confidence") + " (confidence " + confidence + " — keeping block).",
        confidence: confidence,
        model: model,
      };
    }
    var slogan =
      /always blocked|no quinn override|no temp allow|will not override/i.test(whyText);
    var genuineWhy =
      /\b(search engine|web search|does not host porn|does not (host|contain) (any )?(porn|hentai|adult)|primarily used for web search|encyclopedia|documentation|code host)\b/i.test(
        whyText
      ) &&
      !/\b(hosts? porn|is (a )?(porn|tube|cam)|adult (media|gallery)|block(ed|ing)? (this|the) (site|page))\b/i.test(
        whyText
      );
    if (
      !allow &&
      researchHost &&
      !explicitAppeal &&
      !adultAppeal &&
      (slogan ||
        genuineWhy ||
        (evidence && evidence.useCase === "web_search") ||
        (typeof noteLooksResearch === "function" && noteLooksResearch(note)))
    ) {
      return {
        allow: true,
        why: whyText || "Research / search host — opened for this appeal. Known tubes stay blocked.",
        confidence: Math.max(confidence, 0.9),
        model: model,
        source: "appeal_override",
      };
    }
    return {
      allow: allow,
      why: whyText || (allow ? "Looks genuine." : "Looks like adult content."),
      confidence: confidence,
      model: model,
    };
  } catch (e) {
    return {
      allow: false,
      why: "Quinn unreachable — start Ollama (qwen2.5:3b) in Focus Settings → LLM host.",
      error: e && e.message ? e.message : "unreachable",
    };
  }
}

async function handlePornAppealRequest(msg) {
  var host = normalizeHost(msg.host || "");
  var url = String(msg.url || "");
  var reason = String(msg.reason || "porn");
  var note = String(msg.note || "");
  var title = String(msg.title || "");
  var excerpt = String(msg.excerpt || "");
  if (!host) return { ok: false, error: "No host", allow: false };

  var verdict = await askQuinnAppeal({
    host: host,
    url: url,
    reason: reason,
    note: note,
    title: title,
    excerpt: excerpt,
  });

  try {
    await nativeCall(
      {
        type: "log_appeal",
        host: host,
        url: url.slice(0, 400),
        reason: reason,
        allow: Boolean(verdict && verdict.allow),
        why: String((verdict && verdict.why) || "").slice(0, 240),
        confidence: Number((verdict && verdict.confidence) || 0),
        note: note.slice(0, 200),
      },
      800
    );
  } catch (eLog) {
    /* optional */
  }

  if (!verdict || !verdict.allow) {
    return {
      ok: true,
      allow: false,
      host: host,
      why: (verdict && verdict.why) || "Quinn kept the block.",
      confidence: verdict && verdict.confidence,
      hard_deny: Boolean(verdict && verdict.hard_deny),
      error: verdict && verdict.error,
    };
  }

  var nowA = Date.now();
  var untilA =
    nowA + (typeof APPEAL_ALLOW_MS === "number" ? APPEAL_ALLOW_MS : 10 * 60 * 1000);
  var entry = {
    host: host,
    until: untilA,
    why: String(verdict.why || "Quinn false-positive grant").slice(0, 240),
    by: "qwen",
  };
  appealAllowsCache =
    typeof upsertAppealAllow === "function"
      ? upsertAppealAllow(appealAllowsCache, entry, nowA)
      : (appealAllowsCache || []).concat([entry]);
  var keyA = APPEAL_ALLOW_STORAGE_KEY || "appealAllows";
  var payloadA = {};
  payloadA[keyA] = appealAllowsCache;
  payloadA.appealAllows = appealAllowsCache;
  try {
    await new Promise(function (resolve) {
      extAPI.storage.local.set(payloadA, resolve);
    });
  } catch (eSet) {
    /* ignore */
  }
  try {
    invalidateGetModeCache(host);
  } catch (eInv) {
    /* ignore */
  }
  // Refresh DNR fingerprint so appeal state is tracked (force tubes stay in DNR).
  try {
    void syncDeclarativePornBlock({ force: true });
  } catch (eDnr) {
    /* ignore */
  }

  return {
    ok: true,
    allow: true,
    host: host,
    until: untilA,
    why: entry.why,
    confidence: verdict.confidence,
    model: verdict.model,
  };
}

// ----- end background.js -----
