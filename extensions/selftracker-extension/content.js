// ============================================================
// SelfTracker — content.js  v2.0
// Deep behavioral spy: generic tracking + Scalar & YouTube
// deep-dive scrapers for course/video progress
// ============================================================

(function () {
  if (window.__selfTrackerLoaded) return;
  window.__selfTrackerLoaded = true;

  const timers = [];
  let trackingStopped = false;

  /** Extension reload invalidates chrome.runtime on open tabs — stop quietly. */
  function safeSendMessage(payload) {
    if (trackingStopped) return;
    try {
      if (!chrome.runtime?.id) {
        stopTracking();
        return;
      }
      chrome.runtime.sendMessage(payload, () => {
        const err = chrome.runtime.lastError;
        if (!err) return;
        const msg = err.message || "";
        if (
          msg.includes("invalidated") ||
          msg.includes("Receiving end does not exist") ||
          msg.includes("Could not establish connection")
        ) {
          stopTracking();
        }
      });
    } catch (e) {
      const msg = String(e && e.message ? e.message : e);
      if (msg.includes("invalidated")) {
        stopTracking();
      }
    }
  }

  function stopTracking() {
    if (trackingStopped) return;
    trackingStopped = true;
    timers.forEach((id) => {
      clearInterval(id);
      clearTimeout(id);
    });
    timers.length = 0;
  }

  function trackInterval(fn, ms) {
    const id = setInterval(fn, ms);
    timers.push(id);
    return id;
  }

  function trackTimeout(fn, ms) {
    const id = setTimeout(fn, ms);
    timers.push(id);
    return id;
  }

  const HOST = location.hostname;
  const IS_YOUTUBE = HOST.includes('youtube.com');
  const IS_SCALAR  = HOST.includes('scalar.com') || HOST.includes('scalar.dev');

  // ──────────────────────────────────────────────────────────
  // Generic behavioral state
  // ──────────────────────────────────────────────────────────
  const state = {
    scrollDepthMax: 0,
    scrollEvents: [],
    mouseClicks: 0,
    keystrokes: 0,
    mouseMovements: 0,
    lastMouseTime: 0,
    lastScrollY: window.scrollY,
    lastScrollTime: Date.now(),
    idleStart: Date.now(),
    isIdle: false,
    pageLoadTime: Date.now(),
    textLength: 0,
    wordCount: 0
  };

  // ── Generic text scrape ───────────────────────────────────
  function scrapeContext() {
    const article = document.querySelector('article, main, [role="main"], .content, #content, .post-body');
    const text = article
      ? article.innerText.slice(0, 600)
      : document.body.innerText.slice(0, 600);
    const words = text.trim().split(/\s+/).filter(Boolean);
    state.wordCount = words.length;
    state.textLength = text.length;
    return text.trim().replace(/\n+/g, ' ').slice(0, 300);
  }

  // ──────────────────────────────────────────────────────────
  // SCALAR deep scraper
  // Extracts: current chapter, total chapters, sidebar items,
  // completed items (marked with ✓ or .is-complete etc.)
  // ──────────────────────────────────────────────────────────
  function scrapeScalar() {
    const data = { site: 'scalar' };

    // Page title / current section
    data.page_title = document.title;

    // Current section heading
    const heading = document.querySelector('h1, h2, .section-title, [data-title]');
    data.current_section = heading ? heading.innerText.trim().slice(0, 120) : '';

    // Try to find the sidebar navigation for progress
    const navLinks = Array.from(document.querySelectorAll(
      'nav a, aside a, [class*="sidebar"] a, [class*="menu"] a, [class*="nav"] a, [class*="toc"] a'
    ));

    if (navLinks.length > 0) {
      const allItems = navLinks.map(a => ({
        title: a.innerText.trim().slice(0, 80),
        href: a.getAttribute('href') || '',
        // detect completion via classes or aria
        completed: (
          a.classList.contains('is-complete') ||
          a.classList.contains('completed') ||
          a.classList.contains('done') ||
          a.getAttribute('aria-current') === 'page' ||
          a.querySelector('[class*="check"], [class*="done"], svg') !== null ||
          a.innerText.includes('✓') || a.innerText.includes('✔')
        ),
        active: (
          a.getAttribute('aria-current') === 'page' ||
          a.classList.contains('active') ||
          a.classList.contains('current') ||
          window.location.pathname === a.getAttribute('href')
        ),
      })).filter(item => item.title.length > 1);

      data.total_nav_items = allItems.length;
      data.completed_items = allItems.filter(i => i.completed).length;
      data.active_item = allItems.find(i => i.active)?.title || data.current_section;
      data.completion_percent = allItems.length > 0
        ? Math.round((data.completed_items / allItems.length) * 100)
        : 0;
      data.remaining_items = allItems.length - data.completed_items;
      data.nav_items_preview = allItems.slice(0, 10);
    }

    // Reading progress on current page
    const scrollTop = window.scrollY;
    const docHeight = document.documentElement.scrollHeight - window.innerHeight;
    data.page_scroll_percent = docHeight > 0 ? Math.round((scrollTop / docHeight) * 100) : 0;

    // Visible code blocks count (indicates technical depth)
    data.code_blocks_visible = document.querySelectorAll('pre code, .code-block, [class*="codeBlock"]').length;

    // Estimate reading time left on this page
    const articleText = document.querySelector('article, main, .content');
    if (articleText) {
      const wordCount = articleText.innerText.split(/\s+/).length;
      const readSoFar = Math.round((data.page_scroll_percent / 100) * wordCount);
      data.words_remaining_on_page = Math.max(0, wordCount - readSoFar);
      data.est_mins_remaining_on_page = Math.round(data.words_remaining_on_page / 200);
    }

    return data;
  }

  // ──────────────────────────────────────────────────────────
  // YOUTUBE deep scraper
  // Extracts: video title, channel, duration, watched %,
  // playlist info, chapters, watch state
  // ──────────────────────────────────────────────────────────
  function scrapeYouTube() {
    const data = { site: 'youtube' };

    const video = document.querySelector('video');

    if (video) {
      const duration = video.duration || 0;
      const currentTime = video.currentTime || 0;
      data.video_duration_seconds = Math.round(duration);
      data.video_current_seconds = Math.round(currentTime);
      data.video_watched_percent = duration > 0 ? Math.round((currentTime / duration) * 100) : 0;
      data.video_completed = data.video_watched_percent >= 90;
      data.video_paused = video.paused;
      data.video_muted = video.muted;
    }

    // Title
    const titleEl = document.querySelector('h1.ytd-video-primary-info-renderer, .ytd-watch-metadata h1, h1.style-scope');
    data.video_title = titleEl ? titleEl.innerText.trim().slice(0, 200) : document.title;

    // Channel
    const channelEl = document.querySelector('#channel-name a, .ytd-channel-name a, #owner a');
    data.channel_name = channelEl ? channelEl.innerText.trim() : '';

    // Playlist / series info
    const playlistPanel = document.querySelector('#playlist, ytd-playlist-panel-renderer');
    if (playlistPanel) {
      const playlistTitle = playlistPanel.querySelector('#playlist-title, h3, .title');
      data.playlist_title = playlistTitle ? playlistTitle.innerText.trim().slice(0, 100) : '';

      // Count items in playlist
      const playlistItems = playlistPanel.querySelectorAll('ytd-playlist-panel-video-renderer');
      data.playlist_total = playlistItems.length;

      // Find current video position in playlist
      const currentItem = playlistPanel.querySelector('.selected, [aria-selected="true"], .ytd-playlist-panel-video-renderer[selected]');
      const currentIdx = currentItem
        ? Array.from(playlistItems).indexOf(currentItem)
        : -1;
      data.playlist_current_index = currentIdx >= 0 ? currentIdx + 1 : null;
      data.playlist_videos_remaining = currentIdx >= 0 ? data.playlist_total - currentIdx - 1 : null;
      data.playlist_completion_percent = (currentIdx >= 0 && data.playlist_total > 0)
        ? Math.round(((currentIdx + 1) / data.playlist_total) * 100)
        : 0;
    }

    // Video chapters (if present)
    const chapterEls = document.querySelectorAll('.ytp-chapter-title-content, [class*="chapter"]');
    if (chapterEls.length > 0) {
      data.chapters = Array.from(chapterEls)
        .map(el => el.innerText.trim())
        .filter(t => t.length > 0)
        .slice(0, 10);
      data.chapters_count = data.chapters.length;
    }

    // Currently active chapter
    const activeChapter = document.querySelector('.ytp-chapter-title-content');
    data.current_chapter = activeChapter ? activeChapter.innerText.trim() : '';

    // View count (indicates content relevance/quality signal)
    const viewsEl = document.querySelector('.view-count, .ytd-video-view-count-renderer');
    data.view_count_text = viewsEl ? viewsEl.innerText.trim() : '';

    // Is this a shorts/regular video
    data.is_shorts = location.pathname.startsWith('/shorts');
    data.is_live = !!document.querySelector('.ytp-live-badge, .ytp-live');

    // Classify learning vs leisure by title/channel heuristics
    const titleLower = (data.video_title || '').toLowerCase();
    data.content_type = 
      titleLower.match(/tutorial|course|learn|lecture|how to|guide|explained|introduction|bootcamp|crash course|full stack|javascript|python|react|ml|ai|math/)
        ? 'Educational'
        : titleLower.match(/vlog|daily|day in|prank|reaction|funny|meme|gaming|gameplay/)
          ? 'Leisure'
          : 'Unclassified';

    return data;
  }

  // ── Scroll tracking ──────────────────────────────────────
  function getScrollDepthPercent() {
    const scrollTop = window.scrollY;
    const docHeight = document.documentElement.scrollHeight - window.innerHeight;
    return docHeight > 0 ? Math.round((scrollTop / docHeight) * 100) : 0;
  }

  window.addEventListener('scroll', () => {
    const now = Date.now();
    const currentY = window.scrollY;
    const dt = (now - state.lastScrollTime) || 1;
    const velocity = Math.abs(currentY - state.lastScrollY) / dt;

    state.scrollEvents.push(velocity);
    if (state.scrollEvents.length > 50) state.scrollEvents.shift();

    const depth = getScrollDepthPercent();
    if (depth > state.scrollDepthMax) state.scrollDepthMax = depth;

    state.lastScrollY = currentY;
    state.lastScrollTime = now;
    resetIdle();
  }, { passive: true });

  document.addEventListener('click', () => { state.mouseClicks++; resetIdle(); });
  document.addEventListener('mousemove', () => {
    const now = Date.now();
    if (now - state.lastMouseTime > 500) { state.mouseMovements++; state.lastMouseTime = now; }
    resetIdle();
  }, { passive: true });
  document.addEventListener('keydown', () => { state.keystrokes++; resetIdle(); });

  // ── Idle detection ────────────────────────────────────────
  const IDLE_THRESHOLD = 30000;
  let idleTimer = null;

  function resetIdle() {
    state.isIdle = false;
    clearTimeout(idleTimer);
    idleTimer = setTimeout(() => {
      state.isIdle = true;
      state.idleStart = Date.now();
    }, IDLE_THRESHOLD);
  }
  resetIdle();

  // ── Interaction mode ─────────────────────────────────────
  function getInteractionMode() {
    const avgVel = state.scrollEvents.length
      ? state.scrollEvents.reduce((a, b) => a + b, 0) / state.scrollEvents.length
      : 0;
    if (IS_YOUTUBE) {
      const video = document.querySelector('video');
      if (video && !video.paused) return 'watching_video';
    }
    if (state.isIdle) return 'idle';
    if (state.keystrokes > 20) return 'writing';
    if (state.mouseClicks > 10) return 'active_clicking';
    if (avgVel > 3) return 'skimming';
    if (avgVel > 0.3) return 'reading';
    if (state.mouseMovements < 3 && !state.isIdle) return 'passive_watching';
    return 'browsing';
  }

  // ── Build and send snapshot ──────────────────────────────
  function sendSnapshot() {
    if (trackingStopped) return;
    // Active tab only: background tabs / hidden documents do not emit telemetry.
    if (typeof document !== "undefined" && document.visibilityState === "hidden") return;
    // Local CALT SPA is owned by study-presence heartbeats, not extension telemetry.
    try {
      var h = (location.hostname || "").toLowerCase();
      if (h === "localhost" || h === "127.0.0.1") return;
    } catch (e) {
      /* ignore */
    }
    const basePayload = {
      interaction_mode: getInteractionMode(),
      scroll_depth_percent: state.scrollDepthMax,
      scroll_velocity_avg: state.scrollEvents.length
        ? parseFloat((state.scrollEvents.reduce((a, b) => a + b, 0) / state.scrollEvents.length).toFixed(3))
        : 0,
      mouse_clicks: state.mouseClicks,
      keystrokes: state.keystrokes,
      mouse_movements: state.mouseMovements,
      is_idle: state.isIdle,
      time_on_page_seconds: Math.round((Date.now() - state.pageLoadTime) / 1000),
      page_title: document.title,
      scraped_text_preview: scrapeContext(),
      word_count_visible: state.wordCount,
    };

    // Merge deep-scrape data for special sites
    let deepData = {};
    if (IS_SCALAR)  deepData = scrapeScalar();
    if (IS_YOUTUBE) deepData = scrapeYouTube();

    safeSendMessage({
      type: 'BEHAVIORAL_UPDATE',
      data: { ...basePayload, ...deepData }
    });

    // Reset per-interval counters
    state.mouseClicks = 0;
    state.keystrokes = 0;
    state.mouseMovements = 0;
    state.scrollDepthMax = getScrollDepthPercent();
  }

  // For YouTube, also listen to video time updates to catch completions
  if (IS_YOUTUBE) {
    const attachVideoListener = () => {
      const video = document.querySelector('video');
      if (video && !video.__selfTrackerBound) {
        video.__selfTrackerBound = true;
        video.addEventListener('ended', () => {
          safeSendMessage({
            type: 'BEHAVIORAL_UPDATE',
            data: { ...scrapeYouTube(), event: 'VIDEO_COMPLETED', page_title: document.title }
          });
        });
        // Also send at 25%, 50%, 75% milestones
        let milestonesSent = new Set();
        video.addEventListener('timeupdate', () => {
          const pct = video.duration > 0 ? Math.round((video.currentTime / video.duration) * 100) : 0;
          [25, 50, 75].forEach(milestone => {
            if (pct >= milestone && !milestonesSent.has(milestone)) {
              milestonesSent.add(milestone);
              safeSendMessage({
                type: 'BEHAVIORAL_UPDATE',
                data: { ...scrapeYouTube(), event: `VIDEO_${milestone}PCT`, page_title: document.title }
              });
            }
          });
        });
      }
    };
    // YT is a SPA — retry attaching on DOM changes
    trackInterval(attachVideoListener, 2000);
  }

  trackInterval(sendSnapshot, 30000);
  trackTimeout(sendSnapshot, 5000);

  // Jarvis live caption — draggable toast + word typewriter (voice is desktop TTS)
  try {
    var jarvisDrag = { active: false, ox: 0, oy: 0, pid: null };
    var jarvisTypeTimer = null;

    function bindJarvisDrag(el) {
      if (el.__calt_drag_bound) return;
      el.__calt_drag_bound = true;

      function clientXY(ev) {
        if (ev.touches && ev.touches[0]) {
          return { x: ev.touches[0].clientX, y: ev.touches[0].clientY };
        }
        return { x: ev.clientX, y: ev.clientY };
      }

      function onPointerDown(ev) {
        if (ev.pointerType === "mouse" && ev.button !== 0) return;
        var r = el.getBoundingClientRect();
        var p = clientXY(ev);
        jarvisDrag.active = true;
        jarvisDrag.ox = p.x - r.left;
        jarvisDrag.oy = p.y - r.top;
        jarvisDrag.pid = ev.pointerId != null ? ev.pointerId : null;
        el.style.setProperty("right", "auto", "important");
        el.style.setProperty("left", r.left + "px", "important");
        el.style.setProperty("top", r.top + "px", "important");
        el.style.cursor = "grabbing";
        try {
          if (ev.pointerId != null && el.setPointerCapture) {
            el.setPointerCapture(ev.pointerId);
          }
        } catch (e) {
          /* ignore */
        }
        ev.preventDefault();
        ev.stopPropagation();
      }

      function onPointerMove(ev) {
        if (!jarvisDrag.active) return;
        if (jarvisDrag.pid != null && ev.pointerId != null && ev.pointerId !== jarvisDrag.pid) {
          return;
        }
        var p = clientXY(ev);
        var maxX = Math.max(8, window.innerWidth - el.offsetWidth - 8);
        var maxY = Math.max(8, window.innerHeight - el.offsetHeight - 8);
        var x = Math.max(8, Math.min(maxX, p.x - jarvisDrag.ox));
        var y = Math.max(8, Math.min(maxY, p.y - jarvisDrag.oy));
        el.style.setProperty("left", x + "px", "important");
        el.style.setProperty("top", y + "px", "important");
        el.style.setProperty("right", "auto", "important");
        ev.preventDefault();
        ev.stopPropagation();
      }

      function onPointerUp(ev) {
        if (!jarvisDrag.active) return;
        jarvisDrag.active = false;
        el.style.cursor = "grab";
        try {
          if (ev.pointerId != null && el.releasePointerCapture) {
            el.releasePointerCapture(ev.pointerId);
          }
        } catch (e) {
          /* ignore */
        }
        try {
          var r = el.getBoundingClientRect();
          chrome.storage.local.set({
            jarvisToastPos: { left: Math.round(r.left), top: Math.round(r.top) },
          });
        } catch (e) {
          /* ignore */
        }
        ev.stopPropagation();
      }

      el.addEventListener("pointerdown", onPointerDown, true);
      el.addEventListener("pointermove", onPointerMove, true);
      el.addEventListener("pointerup", onPointerUp, true);
      el.addEventListener("pointercancel", onPointerUp, true);
      // Fallback for older engines
      el.addEventListener("mousedown", onPointerDown, true);
      el.addEventListener("touchstart", onPointerDown, { capture: true, passive: false });
      window.addEventListener("mousemove", onPointerMove, true);
      window.addEventListener("touchmove", onPointerMove, { capture: true, passive: false });
      window.addEventListener("mouseup", onPointerUp, true);
      window.addEventListener("touchend", onPointerUp, true);
    }

    function ensureJarvisEl() {
      var el = document.getElementById("__calt_jarvis_caption");
      if (!el) {
        el = document.createElement("div");
        el.id = "__calt_jarvis_caption";
        el.style.cssText =
          "all:initial;position:fixed!important;z-index:2147483647!important;" +
          "left:auto!important;right:16px!important;top:16px!important;" +
          "max-width:min(420px,92vw)!important;box-sizing:border-box!important;" +
          "background:#0f172af5!important;color:#e2e8f0!important;" +
          "font:13px/1.45 Segoe UI,system-ui,sans-serif!important;" +
          "padding:0!important;border-radius:12px!important;" +
          "border:1px solid #475569!important;box-shadow:0 12px 32px #000c!important;" +
          "cursor:grab!important;user-select:none!important;touch-action:none!important;" +
          "pointer-events:auto!important;display:none;";
        el.innerHTML =
          '<div id="__calt_jarvis_grip" style="all:initial;display:block;padding:8px 12px 2px;' +
          "font:10px/1 Segoe UI,system-ui,sans-serif;letter-spacing:.06em;" +
          "text-transform:uppercase;color:#94a3b8;cursor:grab;pointer-events:none;" +
          'user-select:none;">⠿ Jarvis · drag me</div>' +
          '<div id="__calt_jarvis_body" style="all:initial;display:block;padding:4px 12px 12px;' +
          "font:13px/1.45 Segoe UI,system-ui,sans-serif;color:#e2e8f0;" +
          'pointer-events:none;user-select:none;min-height:1.4em;"></div>';
        (document.body || document.documentElement).appendChild(el);
      }
      bindJarvisDrag(el);
      return el;
    }

    function typeJarvisWords(body, fullText) {
      if (jarvisTypeTimer) {
        clearInterval(jarvisTypeTimer);
        jarvisTypeTimer = null;
      }
      var prefix = "Jarvis: ";
      var words = String(fullText || "")
        .trim()
        .split(/\s+/)
        .filter(Boolean);
      if (!words.length) {
        body.textContent = prefix;
        return;
      }
      body.textContent = prefix;
      var i = 0;
      jarvisTypeTimer = setInterval(function () {
        if (i >= words.length) {
          clearInterval(jarvisTypeTimer);
          jarvisTypeTimer = null;
          return;
        }
        body.textContent = prefix + words.slice(0, i + 1).join(" ");
        i += 1;
      }, 280);
    }

    chrome.runtime.onMessage.addListener(function (msg) {
      if (!msg || msg.type !== "JARVIS_LINE") return;
      var text = String(msg.text || "").trim();
      if (!text) return;
      text = text.slice(0, 220);
      var el = ensureJarvisEl();
      var body = document.getElementById("__calt_jarvis_body") || el;
      try {
        chrome.storage.local.get(["jarvisToastPos"], function (res) {
          var pos = res && res.jarvisToastPos;
          if (pos && typeof pos.left === "number" && typeof pos.top === "number") {
            el.style.setProperty("right", "auto", "important");
            el.style.setProperty("left", Math.max(8, pos.left) + "px", "important");
            el.style.setProperty("top", Math.max(8, pos.top) + "px", "important");
          }
        });
      } catch (e) {
        /* ignore */
      }
      el.style.setProperty("display", "block", "important");
      // Skip restarting typewriter if the same line is already fully shown.
      if (el.__lastJarvisText === text && body.textContent && body.textContent.indexOf(text) >= 0) {
        clearTimeout(el.__hideTimer);
        var hideSame = Math.max(7000, 1200 + text.split(/\s+/).length * 280);
        el.__hideTimer = setTimeout(function () {
          el.style.setProperty("display", "none", "important");
          el.__lastJarvisText = "";
        }, hideSame);
        return;
      }
      el.__lastJarvisText = text;
      typeJarvisWords(body, text);
      clearTimeout(el.__hideTimer);
      var hideMs = Math.max(7000, 1200 + text.split(/\s+/).length * 280);
      el.__hideTimer = setTimeout(function () {
        el.style.setProperty("display", "none", "important");
        el.__lastJarvisText = "";
      }, hideMs);
    });
  } catch (e) {
    /* ignore */
  }
})();
