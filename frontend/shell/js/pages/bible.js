import { enforcerCmd, bridgeAvailable } from "../bridge.js";
import { bibleDoneToday } from "../mirrors.js";
import { navigate } from "../router.js";

/**
 * @param {HTMLElement} app
 * @param {object} status
 * @param {() => void} onRefresh
 */
export async function renderBible(app, status, onRefresh) {
  const done = bibleDoneToday(status.softland, status.loop);
  let devotion = null;
  if (bridgeAvailable()) {
    const r = await enforcerCmd("bible.devotion.today", {}, 4000);
    if (r && r.ok !== false && r.devotion) devotion = r.devotion;
  }

  const chapter =
    devotion?.morning_chapter ||
    devotion?.assigned ||
    devotion?.morning ||
    null;
  const label = chapter
    ? `${chapter.book || chapter.book_name || "Chapter"} ${chapter.chapter ?? chapter.ch ?? ""}`.trim()
    : "today’s assigned chapter";

  app.innerHTML = `
    <div class="card">
      <h2 style="margin-top:0">Bible</h2>
      <p class="muted">Morning gate: mark the chapter done so Confirm can run. SoftLand host gate uses <code>morning_bible</code> until then.</p>
      <p>Assigned: <strong>${label}</strong></p>
      <p>Status: <strong>${done ? "done today" : "pending"}</strong></p>
      <div class="row" style="margin-top:1rem">
        <a class="btn" href="/calt-bible/" target="_blank" rel="noopener">Open corpus</a>
        <button type="button" class="btn primary" id="btn-mark" ${done ? "disabled" : ""}>
          ${done ? "Already marked" : "Mark morning done"}
        </button>
        <button type="button" class="btn" id="btn-next">Next: Plan</button>
      </div>
      <p class="error" id="bible-err" hidden></p>
    </div>
  `;

  document.getElementById("btn-next").onclick = () => navigate("plan");
  const mark = document.getElementById("btn-mark");
  if (mark && !done) {
    mark.onclick = async () => {
      mark.disabled = true;
      const err = document.getElementById("bible-err");
      err.hidden = true;
      const r = await enforcerCmd(
        "bible.devotion.done",
        { slot: "morning", done: true },
        5000,
      );
      if (!r || r.ok === false || r.error) {
        err.textContent = r?.error || "Mark failed";
        err.hidden = false;
        mark.disabled = false;
        return;
      }
      navigate("plan");
      onRefresh();
    };
  }
}
