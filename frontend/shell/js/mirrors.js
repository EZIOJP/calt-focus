function dataUrl(name) {
  const host = location.hostname;
  if (host === "calt.app" || host.endsWith(".calt.app")) {
    return `https://calt-data.app/${name}`;
  }
  return `/calt-data/${name}`;
}

export async function fetchJson(name) {
  try {
    const r = await fetch(dataUrl(name), { cache: "no-store" });
    if (!r.ok) return null;
    return await r.json();
  } catch {
    return null;
  }
}

export function localYmd() {
  const d = new Date();
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  return `${y}-${m}-${day}`;
}

export function bibleDoneToday(softland, loop) {
  if (loop?.bible_done) return true;
  const goals = softland?.goals;
  if (!goals?.bible_done) return false;
  return goals.bible_done_for_date === localYmd();
}
