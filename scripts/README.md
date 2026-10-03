# CALT Productivity scripts

Build / install / run helpers for the **C++ Productivity product** (`calt_enforcer`, `calt_focus`, `calt_msg_host`). Not Study (Python :8000).

**Solo pack (Phase 1):** SoftLand decide + browser track + OS kills work with Study API stopped. Focus Settings / live ledger still need `:8000`. Design: `docs/superpowers/specs/2026-09-08-calt-productivity-solo-pack-design.md`.

## Layout

| Folder | What |
|--------|------|
| `build/` | CMake builds + fetch deps (SQLite, WebView2) |
| `install/` | Windows Service / Task Scheduler / native messaging registration |
| `run/` | Daily Focus launch + console enforcer smoke |
| `installer/` | Inno Setup packaging only (`INSTALLER.md`, `.iss`, `installer_payload/`) |

## What to run

```bat
rem Build
scripts\desktop_tracker\build\build_native_enforcer.bat
scripts\desktop_tracker\build\build_native_focus.bat
scripts\desktop_tracker\build\build_calt_msg_host.bat

rem Automated update (UI + classify_rules + natives; locked exe → *.exe.new)
powershell -File scripts\desktop_tracker\build\update_calt_productivity.ps1
rem After SoftLand off + Disarm + processes exit (or tray → Apply pending update):
scripts\desktop_tracker\build\apply_pending_update.bat

rem Install (Admin service once, or Task Scheduler no-admin)
powershell -File scripts\desktop_tracker\install\install_native_enforcer.ps1
powershell -File scripts\desktop_tracker\install\install_enforcer_service.ps1 -Start
powershell -File scripts\desktop_tracker\install\install_calt_msg_host.ps1 -ExtensionIds @('<GATE_ID>','<SELFTRACKER_ID>')

rem Daily
scripts\desktop_tracker\run\run_calt_desktop.bat
scripts\desktop_tracker\run\run_native_enforcer_console.bat

rem SoftLand / Arm block-mode suite (fixture SoftLand + Arm unit + sync)
python scripts\desktop_tracker\run\block_mode_suite.py
rem Live SoftLand probes + optional Arm fixture kills (restores snapshot)
python scripts\desktop_tracker\run\block_mode_suite.py --live --arm-kills
```

Focus tray: **Update UI** (React only) · **Update stack** (full script) · **Apply pending update && restart**.
## Solo-pack notes

- Enforcer Service Recovery / Task restart: brief **Arm fail-open** while restarting; SoftLand still via msg_host.
- Focus relaunch when SoftLand or Arm on (backoff 3/min).
- Focus shell maps `https://calt-data.app` → `data/productivity/behavior` for offline SoftLand why snapshot.

## Compat shims

Flat paths under this folder still forward to `build\` / `install\` / `run\`.
