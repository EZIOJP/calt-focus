const SCREENS = [
  {
    id: "home",
    title: "Home",
    hash: "#/home",
    source: "frontend/shell/js/pages/home.js",
    paths: [
      "frontend/shell/js/pages/home.js",
      "frontend/shell/js/pages/morning.js",
      "frontend/shell/css/shell.css",
    ],
    blocks: [
      { cls: "hero", label: "Day glance — productive min / bible / plan" },
      { cls: "", label: "Status rows" },
      { cls: "cta", label: "Confirm plan" },
    ],
  },
  {
    id: "calendar",
    title: "Calendar",
    hash: "#/calendar",
    source: "frontend/shell/js/pages/calendar.js",
    paths: ["frontend/shell/js/pages/calendar.js", "frontend/shell/css/shell.css"],
    blocks: [
      { cls: "hero", label: "Day / Week / Month nav" },
      { cls: "wide", label: "Hour grid — planned blocks" },
      { cls: "", label: "Actual overlay (Phase 1b)" },
    ],
  },
  {
    id: "plan",
    title: "Plan",
    hash: "#/plan",
    source: "frontend/shell/js/pages/plan.js",
    paths: ["frontend/shell/js/pages/plan.js"],
    blocks: [
      { cls: "hero", label: "Planning-only drafts" },
      { cls: "wide", label: "Draft blocks list" },
      { cls: "cta", label: "Propose merge" },
    ],
  },
  {
    id: "settings",
    title: "Settings",
    hash: "#/settings",
    source: "frontend/shell/js/pages/settings.js",
    paths: ["frontend/shell/js/pages/settings.js"],
    blocks: [
      { cls: "hero", label: "Overview" },
      { cls: "", label: "SoftLand site lists" },
      { cls: "", label: "Arm exe list + unlock" },
    ],
  },
  {
    id: "bible",
    title: "Bible",
    hash: "#/bible",
    source: "frontend/shell/js/pages/bible.js",
    paths: ["frontend/shell/js/pages/bible.js"],
    blocks: [
      { cls: "hero", label: "Chapter reader (/calt-bible/)" },
      { cls: "wide", label: "Passage body" },
      { cls: "cta", label: "Mark devotion done" },
    ],
  },
  {
    id: "journal",
    title: "Journal",
    hash: "#/journal",
    source: "frontend/shell/js/pages/journal.js",
    paths: ["frontend/shell/js/pages/journal.js"],
    blocks: [
      { cls: "hero", label: "Today entry" },
      { cls: "wide", label: "Editor body" },
    ],
  },
];

const listEl = document.getElementById("screen-list");
const wireTitle = document.getElementById("wire-title");
const wireSource = document.getElementById("wire-source");
const wireCanvas = document.getElementById("wire-canvas");
const preview = document.getElementById("preview");
const notes = document.getElementById("notes");
const notesStatus = document.getElementById("notes-status");
const editPaths = document.getElementById("edit-paths");
const showMorning = document.getElementById("show-morning");
const openShell = document.getElementById("open-shell");

let activeId = "home";
let saveTimer = null;

function notesKey(id) {
  return `calt-focus-wf-notes:${id}`;
}

function renderRail() {
  listEl.innerHTML = "";
  for (const s of SCREENS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = s.title;
    btn.dataset.id = s.id;
    btn.classList.toggle("active", s.id === activeId);
    btn.onclick = () => selectScreen(s.id);
    listEl.appendChild(btn);
  }
}

function renderWireframe(screen) {
  const railItems = SCREENS.map(
    (s) => `<span class="${s.id === screen.id ? "on" : ""}"></span>`,
  ).join("");
  const blocks = screen.blocks
    .map((b) => `<div class="wf-block ${b.cls}">${b.label}</div>`)
    .join("");
  wireCanvas.innerHTML = `
    <div class="wf-device">
      <div class="wf-rail-mock" aria-hidden="true">${railItems}</div>
      <div class="wf-body-mock">${blocks}</div>
    </div>
    <div class="wf-morning-mock ${showMorning.checked ? "show" : ""}" id="morning-mock">
      <div class="wf-block hero">Morning chapter overlay</div>
      <div class="wf-block">Mark done → Confirm plan on Home</div>
      <div class="wf-block cta">Mark chapter done</div>
    </div>
  `;
}

function selectScreen(id) {
  const screen = SCREENS.find((s) => s.id === id) || SCREENS[0];
  activeId = screen.id;
  renderRail();
  wireTitle.textContent = screen.title;
  wireSource.textContent = screen.source;
  renderWireframe(screen);
  preview.src = `../shell/index.html${screen.hash}`;
  openShell.href = `../shell/index.html${screen.hash}`;
  notes.value = localStorage.getItem(notesKey(screen.id)) || "";
  notesStatus.hidden = true;
  editPaths.innerHTML = screen.paths.map((p) => `<li><code>${p}</code></li>`).join("");
  history.replaceState(null, "", `#${screen.id}`);
}

notes.addEventListener("input", () => {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    localStorage.setItem(notesKey(activeId), notes.value);
    notesStatus.hidden = false;
    setTimeout(() => {
      notesStatus.hidden = true;
    }, 900);
  }, 250);
});

showMorning.addEventListener("change", () => {
  const el = document.getElementById("morning-mock");
  if (el) el.classList.toggle("show", showMorning.checked);
});

document.getElementById("btn-reload").onclick = () => {
  preview.src = preview.src;
};

const fromHash = (location.hash || "").replace(/^#/, "");
selectScreen(SCREENS.some((s) => s.id === fromHash) ? fromHash : "home");
