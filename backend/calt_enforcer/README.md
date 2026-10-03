# CALT Native Enforcer (C++) — zero-Python desktop tracker

```text
DESKTOP TRACKER RULE (locked):
calt_enforcer.exe must start, read policy, kill processes, write non-browser
tracked_sessions, hold the ownership lock, and expose status with ZERO
runtime dependency on any Python process.
Python may remain only for the study/SoftLand web backend.
```

**Legal:** Original CALT code. Cold Turkey is reference UX only — no CT source.

## What it does (no Python required)

1. Reads kill policy from SQLite `enforcer_runtime` **and/or** `data/behavior/enforcer_policy.json`
2. If `hard_block_armed` and (`gate_locked` or `incubation_active`) → kill listed exes (~1.5s poll)
3. While locked + `anti_tamper` → also kill Task Manager / common process explorers / `w32tm.exe`
4. Writes non-browser foreground sessions → `tracked_sessions` (browsers skipped — SelfTracker owns tabs)
5. Refreshes `data/behavior/enforcer_owner.lock`
6. Writes **`data/behavior/enforcer_status.json`** every ~2.5s (includes Focus watchdog fields)
7. Strong locks (`timer` / `password` / `phrase`) force armed+locked until conditions are met
8. **Solo pack:** never-kill `calt_focus.exe` / `calt_msg_host.exe`; relaunch Focus when SoftLand **or** Arm is on (max 3 starts / 60s backoff)

## Status file extras (solo pack)

`focus_running`, `softland_or_armed`, `focus_relaunch_count`, `focus_last_relaunch_at`, `focus_relaunch_suppressed`

## Stay-alive / recovery

- **Windows Service** (`install_native_enforcer.ps1`): `sc failure` restart/5000 ×3. While restarting, **Arm kills briefly fail-open**; SoftLand decide still works via `calt_msg_host`.
- **Task Scheduler** (`install_enforcer_service.ps1`): `RestartCount=999` / 1 min interval — same Arm fail-open note.

## Build (Windows)

```bat
scripts\desktop_tracker\build_native_enforcer.bat
```

Needs: CMake + MSVC or LLVM MinGW; network once for SQLite amalgamation.  
Output: `build\Release\calt_enforcer.exe` or `build\calt_enforcer.exe`.

## Anti-tamper (while locked + `anti_tamper: true`)

Merged into the kill list (CALT original — small list):

- `taskmgr.exe`
- `procexp.exe`, `procexp64.exe`
- `processhacker.exe`, `systeminformer.exe`
- `resmon.exe`, `perfmon.exe`
- `w32tm.exe`, `tzutil.exe`

## Morning smoke test

See [docs/superpowers/exports/2026-09-07-morning-smoke.md](../../docs/superpowers/exports/2026-09-07-morning-smoke.md).

```bat
scripts\desktop_tracker\run_native_enforcer_console.bat
REM Focus → Arm notepad.exe → open Notepad → dies
REM Optional: taskkill /F /IM python.exe /T → still kills
```

## Install Windows Service (default stay-alive — Admin once)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\desktop_tracker\install_native_enforcer.ps1
```

`run_calt_desktop.bat` prefers a running `CALTEnforcer` service; console is fallback only.

## Run console (fallback)

```bat
scripts\desktop_tracker\run_native_enforcer_console.bat
```

## Related

- Native messaging ping host: `native/calt_msg_host/` (`build_calt_msg_host.bat`)
- Python `backend.behavior.enforcer_service` is **legacy fallback only** — do not extend it.
- Task Scheduler (no admin): `install_enforcer_service.ps1` (native required unless `-AllowPythonFallback`)
