# CALT Focus shell (C++) — tray + WebView2

```text
CT-class split (pattern only, no CT code):
  calt_focus.exe     = tray + WebView2 control UI (this folder)
  calt_enforcer.exe  = OS kills + stay-alive + non-browser track
```

## UI (HTML shell)

Focus loads prebuilt UI from repo `dist-focus/` via WebView2 (`https://calt.app`).

Source of truth: **`frontend/shell/`** (plain HTML/CSS/JS).

```bat
npm run build:focus
scripts\run\run_calt_desktop.bat
```

`build:focus` copies `frontend\shell` → `dist-focus`.  
`run_calt_desktop.bat` ships the shell automatically if `dist-focus\` is missing.

Edit loop: change files under `frontend/shell/` → `npm run build:focus` → tray **Reload UI**.

SoftLand / Arm / Plan writes go through WebView2 → enforcer named pipe (no Study `:8000`).

Does **not** kill processes. Tooltip may read `enforcer_status.json`.

## Build native shell

```bat
scripts\build\build_native_focus.bat
```

## Tray

**Run (enforcer; prebuilt UI)** · Open Calendar · Open Plan · Open Focus · Open Settings ·

**Tray (ship note):** icon is a teal dot — Win11 may hide it under the ^ overflow; pin it. If the tray is missing, use the in-app **Focus** menu for Reload UI / Settings / Quit.

| Action | What it does |
|--------|----------------|
| **Reload UI** | Cache-bust reload of current page from `dist-focus` |
| **Update UI** | `npm run build:focus` (shell → `dist-focus`) then reload |
| **Update stack** | Runs `scripts/build/update_calt_productivity.ps1`: `build:focus`, rebuild enforcer/focus/msg-host. Locked exes → `*.exe.new` + pending update |

**Quit rule:** tray Quit is refused while SoftLand is on or hard-block is armed
(balloon explains). SoftLand off + Disarm → Quit stays quit. Use repo-root
`stop_calt_enforcer.bat` in development if the enforcer goes rogue.

Default window: **Home** (`#/home`). Study (notes, GRE, …) is not part of this product.

## Phase 1 boundary

With Study `:8000` down, **blocks + browser track (`track_tab`) + OS kills** still work via
`calt_msg_host` / `calt_enforcer`. Focus Settings/Arm toggles go through the enforcer
gateway.

## Env

| Var | Purpose |
|-----|---------|
| `CALT_REPO` | Repo root override |
| `CALT_DB` | Locate `data/productivity` |
| `CALT_FOCUS_URL` | Force a URL (skips prebuilt) |
