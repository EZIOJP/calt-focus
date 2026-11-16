(function () {
  var status = document.getElementById("status");
  var inspectHint = document.getElementById("inspectHint");
  var inspectToggle = document.getElementById("inspectToggle");
  var api = typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;
  var inspectOn = false;
  var quinnOn = true;
  var secretClicks = 0;
  var secretTimer = null;

  function paint(res) {
    var g = (res && res.gateCache) || {};
    var b = g.browser || {};
    var mode = (b.mode_label || b.mode || "—").toUpperCase();
    var bits = ["Mode: " + mode];
    var src = g.source || "";
    if (src) bits.push("src:" + src);
    if (res && res.nativeHostOk === true) bits.push("msg_host OK");
    else if (res && res.nativeHostOk === false) bits.push("msg_host DOWN");
    var inc = g.incubation || {};
    if (inc.active) {
      bits.push("incubation " + Math.max(0, inc.remaining_sec || 0) + "s");
    } else if (b.incubation_active) {
      bits.push("incubation");
    }
    if (g.reward_day) bits.push("reward day");
    else if (g.day_unlimited) bits.push("goal met");
    if (g.stale || g.degraded) bits.push("stale");
    var desk = g.desktop || {};
    if (desk.enforcer_owns_kills) bits.push("enforcer on");
    inspectOn = Boolean(res && res.inspectMode);
    if (inspectOn) bits.push("INSPECT");
    if (typeof res.quinnEnabled === "boolean") quinnOn = res.quinnEnabled;
    bits.push(quinnOn ? "Quinn ON" : "Quinn OFF");
    var html = bits.join(" · ");
    if (res && res.nativeHostOk === false) {
      html =
        '<div class="warn">Native host missing — run install_calt_msg_host.ps1</div>' + html;
    }
    if (inc.active) {
      html =
        '<div class="warn">Incubation — entertainment blocked. Open CALT Desktop · Focus.</div>' +
        html;
    }
    if (!quinnOn) {
      html +=
        '<div class="warn" style="margin-top:6px">Quinn OFF — turn on in Focus Home. SoftLand still blocks; appeals stay closed.</div>';
    }
    status.innerHTML = html;
    inspectHint.className = inspectOn ? "inspect on" : "inspect";
    inspectToggle.className = inspectOn ? "on" : "";
    inspectToggle.style.display = inspectOn ? "block" : "none";
    inspectToggle.textContent = "Turn Inspect off";
  }

  function refresh() {
    api.runtime.sendMessage({ type: "GET_GATE" }, function (res) {
      paint(res || {});
    });
  }

  function setInspect(on) {
    api.runtime.sendMessage({ type: "SET_INSPECT_MODE", enabled: on }, function () {
      refresh();
    });
  }

  // Secret: click the title 7 times within ~4s to unlock Inspect.
  document.getElementById("title").addEventListener("click", function () {
    secretClicks += 1;
    if (secretTimer) clearTimeout(secretTimer);
    secretTimer = setTimeout(function () {
      secretClicks = 0;
    }, 4000);
    if (secretClicks >= 7) {
      secretClicks = 0;
      setInspect(true);
    }
  });

  document.getElementById("refresh").onclick = function () {
    status.textContent = "Refreshing…";
    api.runtime.sendMessage({ type: "REFRESH_GATE" }, function (res) {
      paint(res || {});
    });
  };

  function paintV2() {
    api.runtime.sendMessage({ type: "GATE_V2_STATUS" }, function (res) {
      var el = document.getElementById("v2status");
      var th = document.getElementById("thresh");
      if (!el) return;
      if (!res || !res.ok) {
        el.textContent = "Gate v2: not loaded — run npm run build in calt-gate-extension";
        return;
      }
      el.textContent =
        "Gate v" +
        (res.version || "?") +
        (res.enabled ? " ON" : " OFF") +
        (res.shadowOnly ? " (shadow)" : "") +
        " · blocklist " +
        (res.blocklist || 0);
      var t = res.thresholds || {};
      if (th) {
        th.textContent =
          "Thresholds (saved): ALLOW < " +
          (t.allowBelow != null ? t.allowBelow : 50) +
          " · WATCH < " +
          (t.watchBelow != null ? t.watchBelow : 80) +
          " · LOCK ≥ that · log scores ≥ " +
          (t.highScoreLogAt != null ? t.highScoreLogAt : 50) +
          (t.savedAt ? " · saved " + new Date(t.savedAt).toLocaleString() : "");
      }
    });
  }

  document.getElementById("whyBtn").onclick = function () {
    api.runtime.sendMessage({ type: "GATE_V2_WHY" }, function (res) {
      var el = document.getElementById("why");
      if (!el) return;
      var hi = (res && res.highScores) || [];
      var rows = (res && res.recent) || [];
      var parts = [];
      var t = (res && res.thresholds) || {};
      parts.push(
        "Thresholds: ALLOW<" +
          (t.allowBelow != null ? t.allowBelow : "?") +
          " WATCH<" +
          (t.watchBelow != null ? t.watchBelow : "?") +
          " log≥" +
          (t.highScoreLogAt != null ? t.highScoreLogAt : "?")
      );
      parts.push("--- Scores ≥ 50 (normal usage) ---");
      if (!hi.length) parts.push("(none yet)");
      else {
        hi.slice(0, 40).forEach(function (r) {
          parts.push(
            (r.local || r.iso || "") +
              " | " +
              (r.score || 0) +
              " " +
              (r.verdict || "?") +
              " | " +
              (r.host || "") +
              (r.mode ? " | " + r.mode : "") +
              " | " +
              ((r.reasons || []).slice(0, 3).join(", ") || "")
          );
        });
      }
      parts.push("--- Recent ---");
      rows.slice(0, 20).forEach(function (r) {
        parts.push(
          (r.local || r.iso || "") +
            " | " +
            (r.score || 0) +
            " " +
            (r.verdict || "?") +
            " | " +
            (r.host || "")
        );
      });
      el.textContent = parts.join("\n");
    });
  };

  document.getElementById("blocklistBtn").onclick = function () {
    var el = document.getElementById("v2status");
    if (el) el.textContent = "Refreshing blocklist…";
    api.runtime.sendMessage({ type: "GATE_V2_REFRESH_BLOCKLIST" }, function (res) {
      paintV2();
      if (el && res) {
        el.textContent +=
          " · refresh " + (res.ok ? "ok " + res.size : "kept last-good") +
          (res.errors && res.errors.length ? " (" + res.errors[0] + ")" : "");
      }
    });
  };

  inspectToggle.onclick = function () {
    setInspect(false);
  };

  refresh();
  paintV2();
})();
