# CALT Focus — finalized product

**Status:** Owner-locked · 2026-11-16  
**Door:** `calt_focus.exe` only (WebView2 → `dist-focus`)

## What Focus is

A **desktop productivity shell**: SoftLand site gates, Arm kill list, Plan/Calendar, Bible/Journal mornings, Gate extension SoftLand, wearables (ZeppBridge preferred), export/import, and light **body care** (sit/stand + eye focus).

Not Study: no GRE, notes banks, math coursework, or Python SoftLand on `:8000`.

## Owns (locked)

| Surface | Runtime |
|---------|---------|
| SoftLand ≠ Arm | `calt_enforcer` + mirrors |
| Plan / Calendar / Routines | enforcer SQLite + mirrors |
| Bible / Journal / morning gates | enforcer + overlays |
| Browser SoftLand | Gate + `calt_msg_host` |
| Wearables | **ZeppBridge** pull → mirrors; CALT Sync `:8765` optional |
| Export / import SoftLand lists / update check | Settings → More |
| **Body care** | Sit/stand reminders + eye rest + optional webcam **attention reader** (local) |

## Explicit non-goals

- Cold Turkey / Electron / Qt shell  
- Python enforcer for kill/track/status  
- Study plugins, GRE, mermaid, full Life “hub graph”  
- Replacing SoftLand with “willpower only”

## Body care (the one addition)

| Piece | Behavior |
|-------|----------|
| **Sit / stand** | Local interval: after N minutes seated (input idle), notify to stand; optional stand hold; daily stand hint from wearable when present |
| **Eye rest** | 20–20–20 style: every N minutes, short “look away” nudge |
| **Eye focus (attention reader)** | Optional webcam attention % / attitude via existing face-tracker models — **opt-in**, off by default, no SoftLand coupling |

Prefs: `localStorage` `calt:bodycare:*` (+ optional `behavior/body_care.json` mirror later). No enforcer required for reminders.

## Architecture reminder

```text
UI (React) ──reads──► calt-data mirrors
         ──writes──► enforcerNamedPipe (SoftLand/Arm/plan/…)
Wearables ◄── ZeppBridge DB pull (preferred)
Body care ◄── timers + optional webcam (client-only)
```

## Build

- Enforcer: `scripts\build\build_native_enforcer.bat`  
- UI: Study `npm run build:focus` → `sync:focus-ui`  
- Focus shell: `backend\calt_focus\build\calt_focus.exe`
