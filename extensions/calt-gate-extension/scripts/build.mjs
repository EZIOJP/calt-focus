import * as esbuild from "esbuild";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");

function bumpVersion() {
  const manPath = path.join(root, "manifest.json");
  const man = JSON.parse(fs.readFileSync(manPath, "utf8"));
  const parts = String(man.version || "2.0.0").split(".").map((x) => parseInt(x, 10) || 0);
  while (parts.length < 3) parts.push(0);
  parts[2] += 1;
  man.version = parts.join(".");
  // Ensure v2 permissions
  const perms = new Set(man.permissions || []);
  ["storage", "alarms", "tabs", "webNavigation", "declarativeNetRequest", "notifications", "nativeMessaging", "offscreen"].forEach(
    (p) => perms.add(p)
  );
  man.permissions = Array.from(perms);
  man.content_security_policy = {
    extension_pages: "script-src 'self' 'wasm-unsafe-eval'; object-src 'self';",
  };
  man.background = { service_worker: "service_worker.js", type: "module" };
  // Keep classic SW if module fails on older Edge — use bundled IIFE instead
  man.background = { service_worker: "service_worker.js" };
  man.content_scripts = [
    {
      matches: ["<all_urls>"],
      js: ["gate_policy.js", "content_score.js", "content_v2.js"],
      run_at: "document_idle",
      all_frames: true,
      match_about_blank: true,
    },
  ];
  man.web_accessible_resources = [
    {
      resources: [
        "locked.html",
        "locked.js",
        "gate_policy.js",
        "icon.png",
        "offscreen.html",
        "offscreen.js",
        "wasm/*",
        "nsfwjs/*",
        "nsfwjs/mobilenet_v2_mid/*",
      ],
      matches: ["<all_urls>"],
    },
  ];
  fs.writeFileSync(manPath, JSON.stringify(man, null, 2) + "\n");
  return man.version;
}

async function build() {
  const version = bumpVersion();

  await esbuild.build({
    entryPoints: [path.join(root, "src/v2_globals.js")],
    bundle: true,
    outfile: path.join(root, "v2_bundle.js"),
    format: "iife",
    platform: "browser",
    target: ["chrome109"],
    legalComments: "none",
  });

  await esbuild.build({
    entryPoints: [path.join(root, "src/offscreen_main.js")],
    bundle: true,
    outfile: path.join(root, "offscreen.js"),
    format: "esm",
    platform: "browser",
    target: ["chrome109"],
    legalComments: "none",
  });

  // content_v2 is already IIFE source — copy
  fs.copyFileSync(path.join(root, "src/content_v2.js"), path.join(root, "content_v2.js"));

  const policy = fs.readFileSync(path.join(root, "gate_policy.js"), "utf8");
  const v2 = fs.readFileSync(path.join(root, "v2_bundle.js"), "utf8");
  const bg = fs.readFileSync(path.join(root, "background.js"), "utf8");
  const out =
    "// AUTO-BUILT by scripts/build.mjs — Gate v" +
    version +
    "\n\n// ----- begin gate_policy.js -----\n" +
    policy.trimEnd() +
    "\n\n// ----- end gate_policy.js -----\n\n// ----- begin v2_bundle.js -----\n" +
    v2.trimEnd() +
    "\n\n// ----- end v2_bundle.js -----\n\n// ----- begin background.js -----\n" +
    bg.trimEnd() +
    "\n\n// ----- end background.js -----\n";
  fs.writeFileSync(path.join(root, "service_worker.js"), out);
  console.log("Built Gate v" + version);
}

build().catch((e) => {
  console.error(e);
  process.exit(1);
});
