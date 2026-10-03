import { currentRoute, setActiveNav } from "./router.js";
import { installStatusPushListener, loadStatus } from "./status-bus.js";
import { renderMorning } from "./pages/morning.js";
import { renderHome } from "./pages/home.js";
import { renderCalendar } from "./pages/calendar.js";
import { renderPlan } from "./pages/plan.js";
import { renderSettings } from "./pages/settings.js";
import { renderBible } from "./pages/bible.js";
import { renderJournal } from "./pages/journal.js";

const app = document.getElementById("app");
const morningEl = document.getElementById("morning");
const rail = document.getElementById("rail");

let painting = false;
let paintAgain = false;

async function paint(opts) {
  if (painting) {
    paintAgain = true;
    return;
  }
  painting = true;
  try {
    do {
      paintAgain = false;
      const name = currentRoute();
      setActiveNav(rail, name);
      const status = await loadStatus(opts);
      const refresh = () => void paint({ forcePipe: true });
      renderMorning(morningEl, status, refresh);

      switch (name) {
        case "calendar":
          renderCalendar(app, status, refresh);
          break;
        case "plan":
          await renderPlan(app, status, refresh);
          break;
        case "settings":
          await renderSettings(app, status, refresh);
          break;
        case "bible":
          await renderBible(app, status, refresh);
          break;
        case "journal":
          renderJournal(app);
          break;
        case "home":
        default:
          renderHome(app, status, refresh);
          break;
      }
    } while (paintAgain);
  } finally {
    painting = false;
  }
}

window.addEventListener("hashchange", () => void paint());
installStatusPushListener(() => void paint());
if (!location.hash) location.hash = "#/home";
void paint({ forcePipe: true });
