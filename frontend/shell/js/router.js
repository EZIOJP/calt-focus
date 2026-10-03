/** Hash routes: #/home #/calendar #/plan #/settings #/bible #/journal
 * Also accepts legacy React hashes: #/productivity?tab=… #/productivity/focus
 */

export function currentRoute() {
  const raw = (location.hash || "#/home").replace(/^#\/?/, "") || "home";
  const [path, qs] = raw.split("?");
  const tab = new URLSearchParams(qs || "").get("tab");

  if (path === "productivity" || path === "") {
    if (tab === "settings") return "settings";
    if (tab === "calendar") return "calendar";
    if (tab === "plan") return "plan";
    if (tab === "bible") return "bible";
    if (tab === "journal") return "journal";
    return "home";
  }
  if (path === "productivity/focus") return "home";
  return path || "home";
}

export function setActiveNav(rail, name) {
  if (!rail) return;
  rail.querySelectorAll("a[data-route]").forEach((a) => {
    a.classList.toggle("active", a.dataset.route === name);
  });
}

export function navigate(route) {
  location.hash = `#/${route}`;
}
