# CALT Gate (blocker) — v2

Scored, layered SoftLand for Edge. Talks to **`calt_msg_host.exe`**. Image scan = **NSFWJS MobileNetV2Mid on TF.js WASM** (CPU only).

## Build

```bat
cd extensions\calt-gate-extension
npm install
npm run fetch-models
npm run build
npm test
```

`npm run build` regenerates `service_worker.js`, `offscreen.js`, `content_v2.js`, bumps `manifest.json`, and writes `backend/calt_msg_host/src/host_seed.generated.h`.

Then **Reload** the unpacked extension at `edge://extensions`.

Rebuild native SoftLand after seed changes:

```bat
scripts\build\build_native_enforcer.bat
```

(or your usual msg_host build) so `host_seed.generated.h` is compiled in.

## Strict + grace

| Strict | Grace |
|--------|--------|
| PORN_STRICT / sensitive (SoftLand on) hard LOCK | TRUSTED (Claude, GitHub, …) skip keyword locks |
| Adult search query LOCK before results | Recovery queries (`porn addiction help`, `breast cancer`) |
| No 60s temp-allow; appeal cooldown | SEARCH with clean query auto-opens on appeal |
| NSFWJS blur-first on UGC/social/unknown | Bing tracking params (`gs_lcrp`) ignored |
| Weekly StevenBlack + HaGeZi blocklist Set | Seed floor always on if download fails |

## Popup

- **Why?** — last verdicts (host, score, reasons)
- **Refresh blocklist** — pull StevenBlack porn + HaGeZi NSFW now
