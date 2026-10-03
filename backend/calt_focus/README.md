# CALT Focus shell (C++) — tray + WebView2

```text
CT-class split (pattern only, no CT code):
  calt_focus.exe     = tray + WebView2 control UI (this folder)
  calt_enforcer.exe  = OS kills + stay-alive + non-browser track
```

## Design host (hot-reload Focus UI in Cursor)

Edit Productivity UI without rebuilding `dist-focus` every change:

```bat
npm run dev:focus
```

Open **http://127.0.0.1:5180/** (detected as Focus shell). Port **5180** avoids clash with Focus static fallback on **:5174**. Vite serves `data/productivity/behavior/*` at `/calt-data/`. SoftLand/Arm/Plan **writes** still need `calt_focus.exe` (WebView2 → enforcer pipe).

Tray is **Productivity-first** (Open Focus / Calendar / Plan / Settings). Study `:8000` and Vite live under **Advanced (Study / rebuild)** — not required for SoftLand/Arm/Plan.

Ship to the tray shell with `npm run build:focus` (or tray **Update UI**).

## No Vite every day

Build the UI **once**, then Focus loads pages from disk (`file://…/dist-focus/…`):

```bat
npm run build:focus
scripts\desktop_tracker\run_calt_desktop.bat
```

`run_calt_desktop.bat` auto-runs `build:focus` if `dist-focus\` is missing.

- **UI:** precompiled React in `dist-focus/` (no `npm run dev`, no port 5173)
- **API:** SoftLand/Arm via enforcer pipe; Study `:8000` optional (tray → Start API)
- **Dev fallback:** if `dist-focus` is absent, can use Vite `:5173`

Does **not** kill processes. Tooltip may read `enforcer_status.json`.

## Build native shell

```bat
scripts\desktop_tracker\build_native_focus.bat
```

## Tray

**Run (enforcer + API; prebuilt UI)** · Start API · Start Frontend (optional Vite) ·
Open Calendar · Open Plan · Open Focus · Open Settings ·
**Tray (ship note):** icon is a teal dot — Win11 may hide it under the ^ overflow; pin it. If the tray is missing, use the in-app **Focus** menu (top bar) for Reload UI / Settings / Quit.

| Action | What it does |
|--------|----------------|
| **Reload UI** | Cache-bust reload of current Settings/Calendar |
| **Update UI** | `npm run build:focus` then reload (fast path for React-only edits) |
| **Update stack** | Runs `scripts/desktop_tracker/build/update_calt_productivity.ps1`: export `classify_rules.json`, `build:focus`, rebuild enforcer/focus/msg-host. Locked exes → `*.exe.new` + `data/behavior/pending_update.json` |
| **Apply pending update && restart** | SoftLand off + Disarm required. Spawns `apply_pending_update.bat`, quits Focus; bat swaps `.new` → exe and relaunches |

CLI (same as tray Update stack):

```powershell
powershell -File scripts\desktop_tracker\build\update_calt_productivity.ps1
# after SoftLand off + Disarm + Quit Focus/enforcer:
scripts\desktop_tracker\build\apply_pending_update.bat
```

**Quit rule (Phase 1):** tray Quit is refused while SoftLand is on or hard-block is armed
(balloon explains). SoftLand off + Disarm → Quit stays quit. Killing `calt_focus.exe`
while SoftLand-or-Arm is on causes enforcer relaunch (backoff-capped).

Default window: **Calendar**. Study (notes, GRE, …) stays in the browser webapp.

## Phase 1 boundary (solo pack)

With `:8000` down, **blocks + browser track (`track_tab`) + OS kills** still work via
`calt_msg_host` / `calt_enforcer`. Focus Settings/Arm toggles go through the enforcer
gateway. Do not overclaim that Google/wearables/LLM work without their sidecars.

## Env

| Var | Purpose |
|-----|---------|
| `CALT_REPO` | Repo root override |
| `CALT_DB` | Locate `data/behavior` |
| `CALT_FOCUS_URL` | Force a URL (skips prebuilt) |
