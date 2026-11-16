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
| Bible / Journal / morning gates | enforcer + overlays — **Bible → Plan → Apply/Confirm** (plan-first; no Home short-session create) |
| Browser SoftLand | Gate + `calt_msg_host` |
| Wearables | **ZeppBridge** pull → mirrors; CALT Sync `:8765` optional |
| Export / import SoftLand lists / update check | Settings → **Data** |
| **Body care** | Settings → **Day** — sit/stand + eye rest + optional webcam attention/posture (local) |

## Day path (locked)

```text
Bible → Plan (Apply routines / edit blocks) → Confirm → day runs
```

During unconfirmed plan: Plan tab only; **Planning prefs** sheet on the gate (not full Settings). SoftLand/Arm list edits need free day / pass after Confirm. Home does **not** create short/dynamic work sessions (End leftover only).

## Explicit non-goals

- Cold Turkey / Electron / Qt shell  
- Python enforcer for kill/track/status  
- Study plugins, GRE, mermaid, Math/Nutrition routes, CommunityRanks, full Life “hub graph”  
- Study `:8000` SoftLand / distraction-gate HTTP (Gate = native msg_host only)  
- Replacing SoftLand with “willpower only”  
- Home work-session create as the daily focus loop (End leftover only)

## Master-app strip (locked)

| Area | Focus behavior |
|------|----------------|
| Tray | Rebuild / Focus / Quit — no Start API / FE / Math |
| Routes | SoftLand, Arm, Plan, Calendar, Bible, Journal, Settings, Watch — no Math/Nutri/Community |
| Wearables | ZeppBridge pull first; CALT Sync `:8765` optional under details |
| Gate | `studyHttpFallback` forced off; clears stored `:8000` opt-in flags |
| Day loop | Bible → Plan → Apply/Confirm; Home does not create short sessions |

## Body care (the one addition)

| Piece | Behavior |
|-------|----------|
| **Sit / stand** | Local interval: after N minutes seated, notify to stand; optional stand hold; daily stand hint from wearable when present. With camera on + **presence pause**: no face freezes the sit clock (stepped away). Without camera: input-idle heuristic. |
| **Eye rest** | 20–20–20 style: every N minutes, short “look away” nudge |
| **Eye focus (attention reader)** | Optional webcam attention % / attitude via existing face-tracker models — **opt-in**, off by default, no SoftLand coupling. Shell-hosted so it stays live outside Settings. |
| **Posture coach** | Same webcam: neck/lean score from head pitch; warn after sustained poor posture (default ~3 min). Not a separate Pose/Electron app. |

Prefs: `localStorage` `calt:bodycare:*` (+ optional `behavior/body_care.json` mirror later). No enforcer required for reminders. No Python activity_tracker bridge in-product (optional research path only).

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
