# CALT Focus — Agent Context

Standalone **Productivity** product. No Study GRE/notes/math. No SoftLand brain on Python `:8000`.

| Owns | Tech |
|------|------|
| SoftLand, Arm, Plan, Calendar, Settings, Bible, Journal, Gate | C++ enforcer + msg-host + `calt_focus` WebView2 |
| Focus UI (target) | HTML/CSS/JS under `frontend/shell/` |
| SoT | `data/productivity/productivity.db` + behavior mirrors |

**Locks:** `softland_enabled` ≠ `hard_block_armed`. Zero Python for arm/kill/track/status. No Electron / Qt. No Cold Turkey code.

**Hybrid FE:** [docs/superpowers/specs/2026-10-03-calt-focus-hybrid-html-shell-design.md](docs/superpowers/specs/2026-10-03-calt-focus-hybrid-html-shell-design.md)

**Build:** `scripts\build\build_native_enforcer.bat` · install scripts under `scripts\install\`

Do not add Study repo paths or `:8000` SoftLand fallbacks as defaults.
