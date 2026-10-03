import { enforcerCmd, bridgeAvailable } from "./bridge.js";
import { fetchJson, bibleDoneToday, localYmd } from "./mirrors.js";

const app = document.getElementById("app");
const morningEl = document.getElementById("morning");
const rail = document.getElementById("rail");

function route() {
  const h = (location.hash || "#/home").replace(/^#\/?/, "") || "home";
  return h.split("?")[0];
}

function setActiveNav(name) {
  rail.querySelectorAll("a[data-route]").forEach((a) => {
    a.classList.toggle("active", a.dataset.route === name);
  });
}

async function loadStatus() {
  const [softland, rollup] = await Promise.all([
    fetchJson("softland_policy.json"),
    fetchJson("day_rollup.json"),
  ]);
  let loop = null;
  if (bridgeAvailable()) {
    const r = await enforcerCmd("day.loop_snapshot", {}, 2500);
    if (r && r.ok !== false && r.loop) loop = r.loop;
  }
  return { softland, rollup, loop };
}

function renderMorning(status) {
  const done = bibleDoneToday(status.softland, status.loop);
  const bedtime = status.loop?.bedtime_active;
  if (done || bedtime || (!status.softland?.goals && !status.loop)) {
    morningEl.classList.add("hidden");
    morningEl.innerHTML = "";
    return;
  }
  if (route() === "bible") {
    morningEl.classList.add("hidden");
    return;
  }
  morningEl.classList.remove("hidden");
  morningEl.innerHTML = `
    <h1>Morning chapter</h1>
    <p>Reading alone doesn’t unlock the day. Mark the chapter done, then confirm your plan — SoftLand and Arm arm together after Confirm.</p>
    <p class="error" id="morning-err" hidden></p>
    <div class="row">
      <button type="button" class="btn" id="btn-open-bible">Open Bible</button>
      <button type="button" class="btn primary" id="btn-mark-done">Mark chapter done</button>
    </div>
  `;
  document.getElementById("btn-open-bible").onclick = () => {
    location.hash = "#/bible";
  };
  document.getElementById("btn-mark-done").onclick = async () => {
    const btn = document.getElementById("btn-mark-done");
    const err = document.getElementById("morning-err");
    btn.disabled = true;
    err.hidden = true;
    const r = await enforcerCmd(
      "bible.devotion.done",
      { slot: "morning", done: true },
      5000,
    );
    if (!r || r.ok === false || r.error) {
      err.textContent = r?.error || "Mark failed — is calt_enforcer running?";
      err.hidden = false;
      btn.disabled = false;
      return;
    }
    location.hash = "#/home";
    void paint();
  };
}

function renderHome(status) {
  const mins = status.rollup?.productive_minutes ?? status.loop?.tracked_productive_minutes ?? 0;
  const focus = status.softland?.goals?.daily_focus_minutes ?? 180;
  const bible = bibleDoneToday(status.softland, status.loop);
  const plan = status.loop?.plan_confirmed;
  app.innerHTML = `
    <div class="card">
      <h2 style="margin-top:0">Home</h2>
      <p class="muted">Today ${localYmd()} · Focus shell (no Study)</p>
      <p>Tracked productive: <strong>${mins}</strong> / ${focus} min</p>
      <p>Bible today: <strong>${bible ? "done" : "pending"}</strong></p>
      <p>Plan confirmed: <strong>${plan ? "yes" : "no"}</strong></p>
      <div class="row" style="margin-top:1rem">
        <button type="button" class="btn primary" id="btn-confirm" ${bible ? "" : "disabled"}>Confirm plan</button>
      </div>
      <p class="error" id="home-err" hidden></p>
    </div>
  `;
  const conf = document.getElementById("btn-confirm");
  if (conf) {
    conf.onclick = async () => {
      conf.disabled = true;
      const err = document.getElementById("home-err");
      const r = await enforcerCmd("day.confirm_plan", {}, 8000);
      if (!r || r.ok === false || r.error) {
        err.textContent = r?.error || "Confirm failed";
        err.hidden = false;
        conf.disabled = false;
        return;
      }
      void paint();
    };
  }
}

function renderStub(title, blurb) {
  app.innerHTML = `
    <div class="card">
      <h2 style="margin-top:0">${title}</h2>
      <p class="muted">${blurb}</p>
    </div>
  `;
}

async function paint() {
  const name = route();
  setActiveNav(name === "bible" ? "bible" : name);
  const status = await loadStatus();
  renderMorning(status);
  if (name === "home" || name === "" || name === "productivity") {
    renderHome(status);
  } else if (name === "calendar") {
    renderStub("Calendar", "Vanilla day/week calendar — Phase 1 (full parity).");
  } else if (name === "settings") {
    renderStub("Settings", "SoftLand / Arm lists — Phase 2 HTML.");
  } else if (name === "bible") {
    renderStub("Bible", "Open corpus via /calt-bible/ — wire reader next.");
  } else {
    renderHome(status);
  }
}

window.addEventListener("hashchange", () => void paint());
void paint();
