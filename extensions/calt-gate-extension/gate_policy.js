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
