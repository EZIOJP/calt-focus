import { chromium } from "playwright";
import { mkdirSync } from "fs";
import { join } from "path";

const out = join(process.cwd(), "docs", "screenshots");
mkdirSync(out, { recursive: true });
const base = "http://127.0.0.1:5180";

const shots = [
  { file: "00-home.png", path: "/productivity?tab=home" },
  { file: "01-overview.png", path: "/productivity?tab=settings&section=overview" },
  { file: "02-blocks-sites.png", path: "/productivity?tab=settings&section=sites" },
  { file: "03-blocks-apps.png", path: "/productivity?tab=settings&section=apps" },
  { file: "04-blocks-filters.png", path: "/productivity?tab=settings&section=filters" },
  { file: "05-unlock.png", path: "/productivity?tab=settings&section=unlock", scroll: "Game sleeper" },
  { file: "06-day-body.png", path: "/productivity?tab=settings&section=day" },
  { file: "07-data.png", path: "/productivity?tab=settings&section=data" },
  { file: "08-advanced.png", path: "/productivity?tab=settings&section=advanced" },
  { file: "10-bible.png", path: "/bible" },
  { file: "11-appearance.png", path: "/productivity?tab=settings&section=appearance" },
];

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({
  viewport: { width: 1440, height: 900 },
  colorScheme: "dark",
});
const page = await context.newPage();

await page.addInitScript(() => {
  try {
    localStorage.setItem("theme", "dark");
    document.documentElement.classList.add("dark");
    document.documentElement.setAttribute("data-theme", "dark");
  } catch {}
});

for (const s of shots) {
  const url = `${base}${s.path}`;
  console.log("goto", url);
  await page.goto(url, { waitUntil: "domcontentloaded", timeout: 60000 });
  await page.waitForTimeout(1500);
  if (s.scroll) {
    await page.getByText(s.scroll, { exact: false }).first().scrollIntoViewIfNeeded().catch(() => {});
    await page.waitForTimeout(400);
  }
  await page.screenshot({ path: join(out, s.file), fullPage: false });
  console.log("wrote", s.file);
}

await browser.close();
console.log("done");
