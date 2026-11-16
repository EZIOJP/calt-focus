import { describe, it, expect } from "vitest";
import { decide, mergeMonotonic } from "../src/decision_engine.js";
import { hostTier } from "../src/host_intel.js";
import { adultTokensInText, webSearchQuery, isRecoverySearchQuery } from "../src/url_tokens.js";
import fixtures from "../fixtures/urls.json";

describe("mergeMonotonic", () => {
  it("does not carry YouTube LOCK onto scaler.com", () => {
    const prev = {
      verdict: "LOCK",
      score: 80,
      hard: "watch_study",
      host: "youtube.com",
      reasons: ["tier:WATCH"],
    };
    const next = {
      verdict: "ALLOW",
      score: 0,
      hard: null,
      host: "scaler.com",
      reasons: ["tier:TRUSTED"],
    };
    const m = mergeMonotonic(prev, next);
    expect(m.verdict).toBe("ALLOW");
    expect(m.host).toBe("scaler.com");
    expect(m.hard).toBe(null);
  });
});

describe("hostTier", () => {
  it("marks tubes PORN_STRICT", () => {
    expect(hostTier("pornhub.com")).toBe("PORN_STRICT");
    expect(hostTier("claude.ai")).toBe("TRUSTED");
    expect(hostTier("bing.com")).toBe("SEARCH");
    expect(hostTier("reddit.com")).toBe("SENSITIVE");
  });
});

describe("tokenizer grace", () => {
  it("ignores Bing tracking milf param", () => {
    const u = "https://www.bing.com/search?q=%2Cilj&gs_lcrp=milf";
    expect(webSearchQuery(u)).toBe(",ilj");
    expect(adultTokensInText(webSearchQuery(u))).toEqual([]);
  });
  it("allows recovery queries", () => {
    expect(isRecoverySearchQuery("porn addiction help")).toBe(true);
    expect(isRecoverySearchQuery("free milf videos")).toBe(false);
  });
  it("does not Scunthorpe on analysis", () => {
    expect(adultTokensInText("analysis report")).toEqual([]);
    expect(adultTokensInText("essex council")).toEqual([]);
  });
});

describe("fixtures", () => {
  function run(row) {
    return decide({
      url: row.url,
      mode: row.mode || "free",
      softlandEnabled: row.softlandEnabled !== false,
      contentScore: row.contentScore || 0,
      contentAction: row.contentAction || "",
    });
  }

  for (const row of fixtures.must_lock) {
    it("LOCK " + row.url, () => {
      const v = run(row);
      expect(v.verdict).toBe("LOCK");
      if (row.hard) expect(v.hard).toBe(row.hard);
    });
  }
  for (const row of fixtures.must_allow) {
    it("ALLOW " + row.url, () => {
      const v = run(row);
      expect(v.verdict).toBe(row.expect);
    });
  }
  for (const row of fixtures.traps) {
    it("trap " + (row.note || row.url), () => {
      const v = run(row);
      expect(v.verdict).toBe(row.expect);
    });
  }
});
