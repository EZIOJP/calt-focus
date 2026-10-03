(function () {
  var status = document.getElementById("status");
  var api = typeof chrome !== "undefined" && chrome.runtime ? chrome : browser;

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
    if (res && res.redirectsEnabled === false) bits.push("redirects OFF");
    var desk = g.desktop || {};
    if (desk.enforcer_owns_kills) bits.push("enforcer on");
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
    status.innerHTML = html;
  }

  function refresh() {
    api.runtime.sendMessage({ type: "GET_GATE" }, function (res) {
      paint(res || {});
    });
  }

  document.getElementById("refresh").onclick = function () {
    status.textContent = "Refreshing…";
    api.runtime.sendMessage({ type: "REFRESH_GATE" }, function (res) {
      paint(res || {});
    });
  };

  document.getElementById("toggle").onclick = function () {
    api.runtime.sendMessage({ type: "GET_GATE" }, function (cur) {
      var next = !(cur && cur.redirectsEnabled !== false);
      api.runtime.sendMessage({ type: "SET_REDIRECTS", enabled: next }, function () {
        refresh();
      });
    });
  };

  refresh();
})();
