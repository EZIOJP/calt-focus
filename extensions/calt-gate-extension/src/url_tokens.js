/**
 * Word-boundary URL / query tokenizer — no Essex/Scunthorpe substring traps.
 */
import { RECOVERY_SEARCH_RE } from "./gate_config.js";

const ADULT_TOKENS = new Set([
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
  "sexcam",
]);

const ADULT_PHRASES = ["hardcore porn", "sex video", "adult video", "live sex", "nude pics"];

export function tokenizeText(s) {
  return String(s || "")
    .toLowerCase()
    .split(/[^a-z0-9]+/g)
    .filter((t) => t.length >= 3);
}

export function adultTokensInText(s) {
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

export function isRecoverySearchQuery(q) {
  return RECOVERY_SEARCH_RE.test(String(q || ""));
}

/** Search engines: judge query params only (ignore gs_lcrp etc.). */
export function webSearchQuery(url) {
  let u;
  try {
    u = new URL(String(url || ""));
  } catch (e) {
    return null;
  }
  const h = u.hostname.toLowerCase().replace(/^www\./, "");
  const searchHost =
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
  const q =
    u.searchParams.get("q") ||
    u.searchParams.get("query") ||
    u.searchParams.get("p") ||
    u.searchParams.get("text") ||
    "";
  const searchPath = u.pathname.indexOf("/search") >= 0 || h.indexOf("duckduckgo.") >= 0;
  if (!searchPath && !q) return null;
  return q;
}

export function urlHasAdultTokens(url, title) {
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

export function isImageSearchUrl(url) {
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
