/**
 * Content: RTA/meta signals, blur-first on WATCH/UGC, per-image classify via SW.
 */
(function () {
  "use strict";
  if (window.__caltGateV2Content) return;
  window.__caltGateV2Content = true;

  const MIN_PX = 150;
  const cache = new Map();
  let blurOn = false;
  let navId = 0;
  let scanning = false;

  function ext() {
    return typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;
  }

  function pageSignals() {
    let rta = false;
    let metaAdult = false;
    try {
      const html = document.documentElement ? document.documentElement.innerHTML.slice(0, 8000) : "";
      if (html.indexOf("RTA-5042-1996-1400-1577-RTA") >= 0) rta = true;
      const metas = document.querySelectorAll('meta[name="rating"], meta[name="Rating"]');
      for (let i = 0; i < metas.length; i++) {
        const c = String(metas[i].getAttribute("content") || "").toLowerCase();
        if (c.indexOf("adult") >= 0 || c.indexOf("mature") >= 0 || c.indexOf("rta") >= 0) metaAdult = true;
      }
    } catch (e) {
      /* ignore */
    }
    return { rta, metaAdult };
  }

  function ensureBlurStyle() {
    if (document.getElementById("calt-gate-v2-blur")) return;
    const s = document.createElement("style");
    s.id = "calt-gate-v2-blur";
    s.textContent =
      "html.calt-gate-watch img, html.calt-gate-watch picture, html.calt-gate-watch video," +
      "html.calt-gate-watch canvas, html.calt-gate-watch [style*='background-image']{" +
      "filter:blur(22px)!important;transition:filter .2s ease}" +
      "html.calt-gate-watch img.calt-clean, html.calt-gate-watch video.calt-clean{" +
      "filter:none!important}";
    (document.head || document.documentElement).appendChild(s);
  }

  function setBlur(on) {
    blurOn = on;
    ensureBlurStyle();
    document.documentElement.classList.toggle("calt-gate-watch", on);
  }

  function eligible(el) {
    try {
      const r = el.getBoundingClientRect();
      if (r.width < MIN_PX || r.height < MIN_PX) return false;
      return true;
    } catch (e) {
      return false;
    }
  }

  async function fetchAsDataUrl(url) {
    const resp = await ext().runtime.sendMessage({ type: "GATE_V2_FETCH_IMAGE", url: url });
    return resp;
  }

  async function classifyEl(el) {
    const src = el.currentSrc || el.src || "";
    if (!src || src.indexOf("data:") === 0 && src.length < 40) return;
    if (cache.has(src)) {
      const c = cache.get(src);
      if (c && c.clean) el.classList.add("calt-clean");
      return;
    }
    try {
      let dataUrl = "";
      if (src.indexOf("data:image") === 0) dataUrl = src;
      else {
        const fetched = await fetchAsDataUrl(src);
        if (!fetched || !fetched.ok) {
          ext().runtime.sendMessage({
            type: "GATE_V2_IMAGE_RESULT",
            scannerOffline: Boolean(fetched && fetched.scannerOffline),
            url: location.href,
            title: document.title,
            navId,
          });
          return;
        }
        dataUrl = fetched.dataUrl;
      }
      const result = await ext().runtime.sendMessage({
        type: "GATE_V2_CLASSIFY_URL",
        dataUrl: dataUrl,
        url: src,
      });
      if (!result || !result.ok) {
        cache.set(src, { clean: false });
        ext().runtime.sendMessage({
          type: "GATE_V2_IMAGE_RESULT",
          scannerOffline: true,
          url: location.href,
          navId,
        });
        return;
      }
      const ph = Number(result.pornHentai) || 0;
      const sexy = Number(result.sexy) || 0;
      const clean = ph < 0.35 && sexy < 0.7;
      cache.set(src, { clean: clean, ph: ph });
      if (clean) el.classList.add("calt-clean");
      ext().runtime.sendMessage({
        type: "GATE_V2_IMAGE_RESULT",
        pornHentai: ph,
        sexy: sexy,
        url: location.href,
        title: document.title,
        navId,
      });
    } catch (e) {
      /* ignore */
    }
  }

  function scanVisible() {
    if (!blurOn || scanning) return;
    scanning = true;
    const imgs = document.querySelectorAll("img, video");
    const batch = [];
    for (let i = 0; i < imgs.length && batch.length < 6; i++) {
      if (eligible(imgs[i]) && !imgs[i].classList.contains("calt-clean")) batch.push(imgs[i]);
    }
    Promise.all(batch.map(classifyEl)).finally(() => {
      scanning = false;
    });
  }

  // Report page signals once
  const sig = pageSignals();
  if (sig.rta || sig.metaAdult) {
    ext().runtime.sendMessage({
      type: "GATE_V2_CONTENT",
      url: location.href,
      title: document.title,
      rta: sig.rta,
      metaAdult: sig.metaAdult,
      score: 0,
      action: "",
    });
  }

  ext().runtime.onMessage.addListener((msg) => {
    if (!msg || msg.type !== "GATE_V2_WATCH") return;
    if (msg.navId != null) navId = msg.navId;
    setBlur(Boolean(msg.blur));
    if (msg.blur) scanVisible();
  });

  const mo = new MutationObserver(() => {
    if (blurOn) {
      ensureBlurStyle();
      scanVisible();
    }
  });
  mo.observe(document.documentElement, { childList: true, subtree: true });

  if ("IntersectionObserver" in window) {
    const io = new IntersectionObserver(
      (entries) => {
        if (!blurOn) return;
        for (let i = 0; i < entries.length; i++) {
          if (entries[i].isIntersecting) void classifyEl(entries[i].target);
        }
      },
      { rootMargin: "150% 0px" }
    );
    const arm = () => {
      document.querySelectorAll("img, video").forEach((el) => {
        if (eligible(el)) io.observe(el);
      });
    };
    arm();
    setInterval(arm, 4000);
  }

  // Ask SW if this host should start in WATCH
  ext().runtime.sendMessage({ type: "GATE_V2_STATUS" }, () => {});
})();
