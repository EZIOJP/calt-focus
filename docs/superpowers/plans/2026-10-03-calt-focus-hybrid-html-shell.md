# Plan: Focus hybrid HTML shell

**Spec:** [docs/superpowers/specs/2026-10-03-calt-focus-hybrid-html-shell-design.md](../specs/2026-10-03-calt-focus-hybrid-html-shell-design.md)

## Phase 0 (first ship)

1. Scaffold `calt-focus/frontend/shell/` (`index.html`, `css/tokens.css`, `js/bridge.js`, `js/router.js`, `js/status-bus.js`)
2. Port Home + morning overlay + sidebar chrome (match current copy/layout)
3. Wire Confirm / bible done / day.loop_snapshot / day_rollup mirrors
4. Dual-build: `build:focus` can emit shell → `dist-focus/` behind flag; smoke with enforcer, Study stopped
5. Cut default WebView entry to shell when smoke green

## Phase 1 (full calendar parity in HTML/JS)

1. **1a** Day+Week grid, CRUD via gateway  
2. **1b** Actual overlay + hour slices  
3. **1c** Pointer DnD move/resize  
4. **1d** Month + display prefs  
5. **1e** Plan-tab drafts / planning-only  
6. Remove React `PlannerCalendar` from Focus entry when 1e green

## Phase 2–3

1. Settings HTML sections
2. Retire Focus React `FocusApp` entry; update READMEs / AGENTS.md

## Out of scope for Phase 0

- Full DnD calendar parity
- Contour sticker / calligraphy controls
- Electron
