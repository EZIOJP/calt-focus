# CALT Focus — Agent Context

Standalone **Productivity** product. No Study GRE/notes/math. No SoftLand brain on Python `:8000`.

| Owns | Tech |
|------|------|
| SoftLand, Arm, Plan, Calendar, Settings, Bible, Journal, Gate | C++ enforcer + msg-host + `calt_focus` WebView2 |
| Focus UI | React SPA → `dist-focus/` (build/sync from Study sibling) |
| SoT | `data/productivity/productivity.db` + behavior mirrors |

**Locks:** `softland_enabled` ≠ `hard_block_armed`. Zero Python for arm/kill/track/status. No Electron / Qt. No Cold Turkey code. No HTML shell UI.

**Build:** `scripts\build\build_native_enforcer.bat` · `npm run build:focus` / `sync:focus-ui` · install under `scripts\install\`

**FE design host:** Study sibling `npm run dev:focus` → `http://127.0.0.1:5180/` (HMR). Source of truth for React is Study `src/`; sync into this repo’s `dist-focus/` for WebView. Do not treat tray Study `:5173` as the Focus design host.

**Settings reference:** every Settings control (presentation + FE + enforcer) → [`docs/SETTINGS.md`](docs/SETTINGS.md).

Do not add Study `:8000` SoftLand fallbacks as defaults.

## How to work here

Prefer a **thoughtful, snappy product** over minimal patches.

- When fixing or touching a surface, also land **small adjacent polish** that clearly improves feel: loading flashes, layout hierarchy, duplicate fetches, expensive render paths, confusing copy, mismatched sibling gates (Bible vs Plan overlays), etc.
- Do **not** wait for a separate ask for obvious UX/perf wins in the area you’re already in.
- Stay coherent with Focus architecture (mirrors for reads; enforcer for track/kill/writes; SoftLand ≠ Arm). Avoid drive-by refactors of unrelated systems or large rewrites without need.
- Default UX bar: snappy + reliable — keep prior UI while refetching (no skeleton wipe), share fetches across sibling panels, avoid O(n²) / per-sample DOM where a busy day will hurt.
