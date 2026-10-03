import { enforcerCmd, bridgeAvailable } from "../bridge.js";
import { fetchJson } from "../mirrors.js";

function esc(s) {
  return String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function hostsToText(arr) {
  return Array.isArray(arr) ? arr.join("\n") : "";
}

function textToHosts(text) {
  return String(text || "")
    .split(/[\n,]+/)
    .map((s) => s.trim().toLowerCase())
    .filter(Boolean);
}

function exesToText(arr) {
  return Array.isArray(arr) ? arr.join("\n") : "";
}

function textToExes(text) {
  return String(text || "")
    .split(/[\n,]+/)
    .map((s) => s.trim().toLowerCase())
    .filter(Boolean)
    .map((s) => (s.endsWith(".exe") ? s : `${s}.exe`));
}

function askConfirm(expected) {
  const v = window.prompt(`Type ${expected} to confirm`);
  return v && v.trim() === expected ? expected : null;
}

function row(label, controlHtml, hint = "") {
  return `
    <div class="set-row">
      <div class="set-label">
        <div class="set-title">${esc(label)}</div>
        ${hint ? `<div class="set-hint">${hint}</div>` : ""}
      </div>
      <div class="set-control">${controlHtml}</div>
    </div>`;
}

function group(title) {
  return `<div class="set-group">${esc(title)}</div>`;
}

function toggle(id, on) {
  return `<label class="set-toggle"><input type="checkbox" id="${id}" ${on ? "checked" : ""}/><span></span></label>`;
}

function btn(id, label, primary = false) {
  return `<button type="button" class="btn ${primary ? "primary" : ""}" id="${id}">${esc(label)}</button>`;
}

function input(id, value, attrs = "") {
  return `<input id="${id}" value="${esc(value)}" ${attrs} />`;
}

function textarea(id, value, rows = 4) {
  return `<textarea id="${id}" rows="${rows}">${esc(value)}</textarea>`;
}

/**
 * Single-tab Settings: every Focus-native control as continuous rows.
 * SoftLand != Arm. Device lock (hosts) is a third layer.
 *
 * @param {HTMLElement} app
 * @param {object} status
 * @param {() => void} onRefresh
 */
export async function renderSettings(app, status, onRefresh) {
  app.innerHTML = `<div class="settings-sheet"><p class="muted">Loading settings...</p></div>`;

  const soft = status?.softland || (await fetchJson("softland_policy.json")) || {};
  const [pol, enfSt, deviceR, rewardR, dayR, ledgerR] = await Promise.all([
    fetchJson("enforcer_policy.json"),
    fetchJson("enforcer_status.json"),
    bridgeAvailable() ? enforcerCmd("device_block.status", {}, 4000) : null,
    bridgeAvailable() ? enforcerCmd("reward.status", {}, 4000) : null,
    bridgeAvailable() ? enforcerCmd("day.status", {}, 4000) : null,
    bridgeAvailable() ? enforcerCmd("ledger.snapshot", {}, 4000) : null,
  ]);

  const dbWrap =
    deviceR && deviceR.ok !== false ? deviceR.device_block || deviceR : {};
  const device = {
    enabled: Boolean(dbWrap.settings?.enabled ?? dbWrap.enabled),
    block_porn: dbWrap.settings?.block_porn !== false,
    block_watch: Boolean(dbWrap.settings?.block_watch ?? dbWrap.block_watch),
    block_social: Boolean(dbWrap.settings?.block_social ?? dbWrap.block_social),
    hosts_applied: Boolean(dbWrap.active),
    needs_sync: Boolean(dbWrap.needs_sync),
  };
  const reward = rewardR && rewardR.ok !== false ? rewardR : {};
  const day = dayR && dayR.ok !== false ? dayR : {};
  const ledgerSec =
    typeof ledgerR?.balance_seconds === "number"
      ? ledgerR.balance_seconds
      : typeof day.balance_seconds === "number"
        ? day.balance_seconds
        : (soft.runtime?.earned_ledger_seconds ?? 0);

  const site = soft.site_rules || {};
  const schedules = soft.schedules || {};
  const goals = soft.goals || {};
  const mode = soft.mode_flags || { study: {}, free: {} };
  const study = mode.study || {};
  const free = mode.free || {};
  const armed = Boolean(pol?.hard_block_armed ?? enfSt?.armed);
  const exes = pol?.exes || [];
  const lockMode = pol?.lock_mode || "none";
  const bedtime = status?.loop?.bedtime_hm || soft.runtime?.bedtime_hm || "";
  const wake = status?.loop?.wake_hm || soft.runtime?.wake_hm || "";
  const freeUntil = soft.runtime?.free_until || status?.loop?.free_until || "";
  const incubation = soft.runtime?.incubation_until || "";
  const windowsJson = JSON.stringify(schedules.windows || [], null, 2);

  const modeFlagKeys = [
    ["block_watch_sites", "Block watch sites"],
    ["block_porn", "Block SoftLand porn heuristic"],
    ["block_social", "Block social"],
    ["block_keywords", "Block keywords"],
    ["block_other", "Block other"],
    ["strict_allowlist", "Strict allowlist"],
  ];

  const studyRows = modeFlagKeys
    .map(
      ([k, lab]) =>
        row(
          `Study - ${lab}`,
          toggle(`mf-study-${k}`, Boolean(study[k])),
          "Applies in SoftLand study mode",
        ),
    )
    .join("");
  const freeRows = modeFlagKeys
    .map(
      ([k, lab]) =>
        row(
          `Free - ${lab}`,
          toggle(`mf-free-${k}`, Boolean(free[k])),
          "Applies in SoftLand free / reward mode",
        ),
    )
    .join("");

  app.innerHTML = `
    <div class="settings-sheet">
      <header class="settings-head">
        <h1>Settings</h1>
        <p class="muted">One tab - SoftLand = sites - Arm = app kills - Device lock = hosts. SoftLand ON != Armed.</p>
        <p class="error" id="set-err" hidden></p>
        <p class="set-ok" id="set-ok" hidden></p>
      </header>

      ${group("Status")}
      ${row("SoftLand (sites)", `<strong>${soft.softland_enabled ? "ON" : "OFF"}</strong>`)}
      ${row("Arm (apps)", `<strong>${armed ? "ARMED" : "disarmed"}</strong> - lock ${esc(lockMode)}`)}
      ${row("Device lock (hosts)", `<strong>${device.enabled ? "ON" : "OFF"}</strong> - applied ${device.hosts_applied ? "yes" : "no"}`)}
      ${row("Free until", `<span class="muted">${esc(freeUntil || "-")}</span>`)}
      ${row("Incubation until", `<span class="muted">${esc(incubation || "-")}</span>`)}
      ${row("Earned ledger", `<strong>${Math.floor(ledgerSec / 60)}</strong> min (${ledgerSec}s)`)}
      ${row(
        "Reward / pass",
        `<strong>${reward.reward_available ?? "-"}</strong> rewards - pass used ${day.passes_used ?? "-"}/${day.passes_limit ?? 2}${day.pass_today ? " - today yes" : ""}`,
      )}
      ${row("Last kill", `<span class="muted">${esc(enfSt?.last_kill_exe || enfSt?.last_kill || "-")}</span>`)}

      ${group("SoftLand master")}
      ${row(
        "Site block enabled",
        `<div class="set-inline">${toggle("sl-enabled", Boolean(soft.softland_enabled))} ${btn("sl-apply", "Apply")}</div>`,
        "Off requires typing UNLOCK. Does not Arm kills.",
      )}

      ${group("Site lists")}
      ${row("Allow (always wins)", textarea("sr-allow", hostsToText(site.allow_extra), 3), "one host per line")}
      ${row("Watch (study-only)", textarea("sr-watch", hostsToText(site.watch_extra), 3))}
      ${row("Block (survives free)", textarea("sr-block", hostsToText(site.block_extra), 3))}
      ${row("Save site lists", btn("sr-save", "Save lists", true))}

      ${group("Schedule")}
      ${row("Schedule enabled", toggle("sch-enabled", Boolean(schedules.enabled)), "First matching window sets study/free")}
      ${row("Windows JSON", textarea("sch-windows", windowsJson, 6), "days 0=Mon ... 6=Sun")}
      ${row("Save schedule", btn("sch-save", "Save schedule", true))}

      ${group("Study mode flags")}
      ${studyRows}
      ${group("Free mode flags")}
      ${freeRows}
      ${row("Save mode flags", btn("mf-save", "Save mode flags", true))}

      ${group("Goals / unlock")}
      ${row(
        "Daily focus minutes",
        `<div class="set-inline">${input("goal-mins", goals.daily_focus_minutes ?? 180, 'type="number" min="1" max="1440"')} ${btn("goal-save", "Save")}</div>`,
      )}
      ${row(
        "Spend earned free",
        `<div class="set-inline">${input("spend-mins", "15", 'type="number" min="15" step="15"')} ${btn("spend-free", "Spend")}</div>`,
        "Uses earned ledger in 15m chunks",
      )}
      ${row(
        "Start incubation",
        `<div class="set-inline">${input("inc-mins", "8", 'type="number" min="1" max="60"')} ${btn("inc-start", "Start")} ${btn("inc-clear", "Clear")}</div>`,
        "Max 1 start / hour",
      )}
      ${row("Claim reward day", btn("reward-claim", "Claim (type REWARD)"), "Needs Bible done today")}
      ${row("Grant day pass", btn("day-pass", "Grant (type PASS)"), "Needs Bible done today")}
      ${row(
        "Emergency wind-down",
        `<div class="set-inline">${input("em-mins", "30", 'type="number" min="5" max="180"')} ${btn("em-go", "Start (type EMERGENCY)")}</div>`,
      )}

      ${group("Bedtime")}
      ${row(
        "Bedtime / wake",
        `<div class="set-inline">${input("bed-hm", bedtime, 'placeholder="23:00"')} ${input("wake-hm", wake, 'placeholder="06:00"')} ${btn("bed-save", "Save")}</div>`,
        "HH:MM local",
      )}

      ${group("Arm (app kills)")}
      ${row(
        "Hard block armed",
        `<div class="set-inline">${toggle("arm-armed", armed)} ${btn("arm-apply", "Apply Arm")}</div>`,
        "Separate from SoftLand. Disarm may need unlock password.",
      )}
      ${row("Anti-tamper extras", toggle("arm-anti", Boolean(pol?.anti_tamper)))}
      ${row(
        "Lock mode",
        `<select id="arm-lock">${["none", "password", "phrase", "timer"]
          .map((m) => `<option value="${m}" ${lockMode === m ? "selected" : ""}>${m}</option>`)
          .join("")}</select>`,
      )}
      ${row("Unlock password", input("arm-pwd", pol?.unlock_password || "", 'type="password" autocomplete="off"'), "Stored for next Arm; also used as provided_unlock on disarm")}
      ${row("Kill list (exes)", textarea("arm-exes", exesToText(exes), 8), "one exe per line")}
      ${row("Save kill list + lock", btn("arm-save-list", "Save Arm policy", true))}

      ${group("Device lock (Windows hosts)")}
      ${row("Enabled", toggle("db-enabled", Boolean(device.enabled ?? false)), "Confirm DEVICE LOCK to flip")}
      ${row("Block porn list", toggle("db-porn", device.block_porn !== false))}
      ${row("Block watch list", toggle("db-watch", Boolean(device.block_watch)))}
      ${row("Block social list", toggle("db-social", Boolean(device.block_social)))}
      ${row(
        "Device lock actions",
        `<div class="set-inline">${btn("db-save", "Save")} ${btn("db-apply", "Apply hosts")} ${btn("db-remove", "Remove hosts")}</div>`,
        "Apply/remove need Admin + DEVICE LOCK",
      )}

      ${group("Dev")}
      ${row(
        "Emergency enforcer stop",
        `<span class="muted">Run <code>stop_calt_enforcer.bat</code> at the repo root</span>`,
      )}
      ${row("Reload settings", btn("set-reload", "Reload"))}
    </div>
  `;

  const errEl = document.getElementById("set-err");
  const okEl = document.getElementById("set-ok");

  function showErr(msg) {
    okEl.hidden = true;
    errEl.textContent = msg || "Failed";
    errEl.hidden = false;
  }
  function showOk(msg) {
    errEl.hidden = true;
    okEl.textContent = msg || "Saved";
    okEl.hidden = false;
  }

  async function run(op, payload, okMsg) {
    if (!bridgeAvailable()) {
      showErr("Enforcer bridge unavailable - is Focus desktop + CALTEnforcer running?");
      return false;
    }
    const r = await enforcerCmd(op, payload, 8000);
    if (!r || r.ok === false || r.error) {
      showErr(r?.error || `${op} failed`);
      return false;
    }
    showOk(okMsg || "OK");
    onRefresh?.();
    return true;
  }

  document.getElementById("set-reload").onclick = () => onRefresh?.();

  document.getElementById("sl-apply").onclick = async () => {
    const enabled = document.getElementById("sl-enabled").checked;
    const payload = { enabled };
    if (!enabled) {
      const c = askConfirm("UNLOCK");
      if (!c) return showErr("Cancelled - UNLOCK required to turn SoftLand off");
      payload.confirm = c;
    }
    await run("softland.set_enabled", payload, enabled ? "SoftLand ON" : "SoftLand OFF");
  };

  document.getElementById("sr-save").onclick = async () => {
    await run(
      "softland.patch_site_rules",
      {
        allow_extra: textToHosts(document.getElementById("sr-allow").value),
        watch_extra: textToHosts(document.getElementById("sr-watch").value),
        block_extra: textToHosts(document.getElementById("sr-block").value),
      },
      "Site lists saved",
    );
  };

  document.getElementById("sch-save").onclick = async () => {
    let windows;
    try {
      windows = JSON.parse(document.getElementById("sch-windows").value || "[]");
    } catch {
      return showErr("Windows JSON is invalid");
    }
    await run(
      "softland.patch_schedules",
      { enabled: document.getElementById("sch-enabled").checked, windows },
      "Schedule saved",
    );
  };

  document.getElementById("mf-save").onclick = async () => {
    const next = { study: { ...study }, free: { ...free } };
    for (const [k] of modeFlagKeys) {
      next.study[k] = document.getElementById(`mf-study-${k}`).checked;
      next.free[k] = document.getElementById(`mf-free-${k}`).checked;
    }
    await run("softland.patch_mode_flags", { mode_flags: next }, "Mode flags saved");
  };

  document.getElementById("goal-save").onclick = async () => {
    const mins = Number(document.getElementById("goal-mins").value);
    if (!Number.isFinite(mins) || mins < 1) return showErr("Invalid focus minutes");
    const next = { ...goals, daily_focus_minutes: Math.round(mins) };
    await run("softland.patch_goals", { goals: next }, "Goal saved");
  };

  document.getElementById("spend-free").onclick = async () => {
    const minutes = Number(document.getElementById("spend-mins").value);
    await run("softland.spend_free", { minutes }, `Spent ${minutes}m free`);
  };

  document.getElementById("inc-start").onclick = async () => {
    const minutes = Number(document.getElementById("inc-mins").value) || 8;
    await run("softland.set_incubation", { minutes }, "Incubation started");
  };
  document.getElementById("inc-clear").onclick = async () => {
    await run("softland.clear_incubation", {}, "Incubation cleared");
  };

  document.getElementById("reward-claim").onclick = async () => {
    const c = askConfirm("REWARD");
    if (!c) return showErr("Cancelled");
    await run("reward.claim", { confirm: c }, "Reward day claimed");
  };
  document.getElementById("day-pass").onclick = async () => {
    const c = askConfirm("PASS");
    if (!c) return showErr("Cancelled");
    await run("day.grant_pass", { confirm: c }, "Day pass granted");
  };
  document.getElementById("em-go").onclick = async () => {
    const c = askConfirm("EMERGENCY");
    if (!c) return showErr("Cancelled");
    const minutes = Number(document.getElementById("em-mins").value) || 30;
    await run("softland.emergency_winddown", { confirm: c, minutes }, "Emergency wind-down active");
  };

  document.getElementById("bed-save").onclick = async () => {
    await run(
      "softland.set_bedtime",
      {
        bedtime_hm: document.getElementById("bed-hm").value.trim(),
        wake_hm: document.getElementById("wake-hm").value.trim(),
      },
      "Bedtime saved",
    );
  };

  async function saveArm(armedOverride) {
    const hard_block_armed =
      typeof armedOverride === "boolean"
        ? armedOverride
        : document.getElementById("arm-armed").checked;
    const pwd = document.getElementById("arm-pwd").value;
    const payload = {
      hard_block_armed,
      gate_locked: hard_block_armed,
      lock_mode: document.getElementById("arm-lock").value || "none",
      anti_tamper: document.getElementById("arm-anti").checked,
      exes: textToExes(document.getElementById("arm-exes").value),
    };
    if (pwd) {
      payload.unlock_password = pwd;
      payload.provided_unlock = pwd;
    }
    await run("arm.set", payload, hard_block_armed ? "Arm ON" : "Arm OFF");
  }

  document.getElementById("arm-apply").onclick = () => saveArm();
  document.getElementById("arm-save-list").onclick = () => saveArm(armed);

  document.getElementById("db-save").onclick = async () => {
    const enabled = document.getElementById("db-enabled").checked;
    const was = Boolean(device.enabled);
    const payload = {
      enabled,
      block_porn: document.getElementById("db-porn").checked,
      block_watch: document.getElementById("db-watch").checked,
      block_social: document.getElementById("db-social").checked,
    };
    if (enabled !== was) {
      const c = askConfirm("DEVICE LOCK");
      if (!c) return showErr("Cancelled - DEVICE LOCK required");
      payload.confirm = c;
    }
    await run("device_block.save", payload, "Device lock saved");
  };
  document.getElementById("db-apply").onclick = async () => {
    const c = askConfirm("DEVICE LOCK");
    if (!c) return showErr("Cancelled");
    await run("device_block.apply", { confirm: c }, "Hosts applied (needs Admin)");
  };
  document.getElementById("db-remove").onclick = async () => {
    const c = askConfirm("DEVICE LOCK");
    if (!c) return showErr("Cancelled");
    await run("device_block.remove", { confirm: c }, "Hosts removed");
  };
}
