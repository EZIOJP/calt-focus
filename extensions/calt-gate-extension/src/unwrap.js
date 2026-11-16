/**
 * Unwrap translate / archive / amp / ?url= indirection. Evaluate destination host.
 */
export function unwrapDestinationUrl(url) {
  let raw = String(url || "");
  try {
    let u = new URL(raw);
    const h = u.hostname.toLowerCase();

    // Google Translate
    if (h === "translate.google.com" || h.endsWith(".translate.goog")) {
      const q = u.searchParams.get("u") || u.searchParams.get("url");
      if (q && /^https?:/i.test(q)) return q;
    }

    // web.archive.org
    if (h === "web.archive.org") {
      const m = u.pathname.match(/\/web\/\d+(?:id_)?\/(https?:\/\/.+)/i);
      if (m) return m[1];
    }

    // Google cache
    if (h === "webcache.googleusercontent.com") {
      const q = u.searchParams.get("q") || "";
      const m = q.match(/cache:[^:]+:(https?:\/\/\S+)/i) || q.match(/(https?:\/\/\S+)/i);
      if (m) return m[1];
    }

    // Common redirect params
    for (const key of ["url", "u", "q", "target", "dest", "redirect", "r"]) {
      const v = u.searchParams.get(key);
      if (v && /^https?:\/\//i.test(v)) {
        try {
          const inner = new URL(v);
          if (inner.hostname && inner.hostname !== u.hostname) return v;
        } catch (e) {
          /* ignore */
        }
      }
    }

    // AMP
    if (h.indexOf(".cdn.ampproject.org") >= 0 || h.indexOf(".ampproject.org") >= 0) {
      const parts = u.hostname.split(".");
      // rough: something.cdn.ampproject.org
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
