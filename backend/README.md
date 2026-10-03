# Focus backend (BE)

Native Productivity runtime. **Product context:** [../README.md](../README.md).

**Rule:** SoftLand decide, Arm/kills, Plan SoT, Bible/Journal SoT need **zero** live Python.

## Binaries

| Binary | Folder | Owns |
|--------|--------|------|
| `calt_enforcer.exe` | `calt-focus/backend/calt_enforcer/` | SoftLand SoT, gateway pipe, kills, sessions, planner, life content, mirrors |
| `calt_focus.exe` | `calt-focus/backend/calt_focus/` | Tray + WebView2 shell; maps `calt.app` / `calt-data.app` / `calt-bible.app` |
| `calt_msg_host.exe` | `calt-focus/backend/calt_msg_host/` | Gate SoftLand `get_mode` + `track_tab` |

Build: `scripts/desktop_tracker/build/build_native_*.bat`  
Data: `data/productivity/{productivity.db,behavior/,bible/}`  
Pipe: `\\.\pipe\calt_enforcer_cmd`

## Key sources

| File | Why |
|------|-----|
| `calt_enforcer/src/cmd_gateway.cpp` | SoftLand / Arm / plan / journal / bible ops |
| `calt_enforcer/src/productivity_store.cpp` | SQLite SoT |
| `calt_enforcer/src/life_content.cpp` | Journal + Bible day → SoftLand `bible_done` |
| `calt_enforcer/src/plan_gate.cpp` | Active plan block → SoftLand free/study |
| `calt_focus/src/tray_icon.cpp` | Productivity tray; Study under Advanced |
| `calt_focus/src/paths.cpp` | Resolves `data/productivity/...` |
| `calt_msg_host/src/softland_decide.cpp` | Browser SoftLand without `:8000` |

Study FastAPI may **read** mirrors; it must not be the SoftLand brain.
