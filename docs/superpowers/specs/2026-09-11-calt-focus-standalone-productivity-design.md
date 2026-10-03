# CALT Focus Standalone Productivity — design

**Date:** 2026-09-11  
**Status:** Owner-locked (v2/v3 decisions) — execute phased  
**Plan source:** Cursor plan `focus_standalone_migration` + refined export  
**Supersedes for door/ownership:** [2026-09-08-future-plan-calendar-watch-cpp.md](../exports/2026-09-08-future-plan-calendar-watch-cpp.md) (unparked with this order)  
**Builds on:** [2026-09-07-calt-productivity-cpp-product-design.md](./2026-09-07-calt-productivity-cpp-product-design.md), [P5 unlock design](./2026-09-08-calt-productivity-p5-native-unlock-accounting-design.md)

---

## 1. Success definition

`calt_focus.exe` is the **only Productivity door**. SoftLand, Arm, unlock accounting, desktop+browser tracking, Calendar/Plan/Watch UI, and Settings work **without** the full Study FastAPI stack (`:8000` study product).

Study (`:8000`) remains content-only: GRE, Bible, Notes, Journal, Math, Study Loop.

### Permanent on-demand Python (not required to launch Focus)

| Concern | Process | Notes |
|---------|---------|--------|
| Google Calendar OAuth/sync | Script / subset uvicorn until Phase 7 polish | No new binary required early |
| Wearables ingest | `tracker_hub` `:8765` | Locked permanent sidecar — phone packages hardcode URL |
| LLM propose / classification-review | Same scripts until Phase 3b | Phase 3b is **after** Phase 7 (optional) |

### Out of scope

- F4 full C++ rewrite of Google/wearables **device-auth** stacks  
- Porting GRE / Bible / Journal / Math / Notes into Focus  
- Reintroducing Python kills (`enforcer_service`)  
- Qt `calt_desktop`

---

## 2. Architecture (target)

```text
calt_focus.exe  (sole Productivity door)
    │  WebView2 → dist-focus → https://calt.app
    │  reads: https://calt-data.app/*.json  (data/behavior mirrors)
    │  writes: enforcerNativeCmd → \\.\pipe\calt_enforcer_cmd
    ▼
calt_enforcer.exe
    ├── SoftLand SoT + unlock + session scoring + planner SQLite
    │     DB: data/vocab_app.db  (productivity_* + planner_* — locked, no productivity.db split)
    ├── publishes softland_policy.json / enforcer_status.json / day rollup mirrors
    └── kills / Arm / desktop sessions
         │
calt_msg_host.exe ◄── Gate (native messaging)  get_mode + track_tab

Optional sidecar scripts (manual tray start):
    Google OAuth  ·  tracker_hub :8765  ·  LLM (until Phase 3b)
```

**Naming (forever):** `softland_enabled` = sites only. `hard_block_armed` = native kill switch. SoftLand never sets Arm.

---

## 3. Process policy

| Rule | Lock |
|------|------|
| Phase sequencing | Strict **0 → 1** (no dual-door window) |
| After Phase 0 | Study productivity nav gone; `/productivity*` = interstitial; **all edits via Focus** |
| Priority if time-short | SoftLand/Arm/Settings → Calendar/Plan UX → sidecar polish |
| DB | Always `data/vocab_app.db` for productivity + planner tables |
| Enforcer unreachable | Hard-block writes + "Start Enforcer" CTA (never silent no-op / never queue) |

---

## 4. Locked decisions (15)

| # | Decision |
|---|----------|
| 1 | Wearables ingest = permanent `:8765` sidecar |
| 2 | Phase 3b native LLM = after Phase 7 (optional) |
| 3 | Enforcer-unreachable = hard block + CTA |
| 4 | DB = keep `vocab_app.db` |
| 5 | Study `/productivity*` = interstitial only (routes stay in bundle) |
| 6 | MakeSettings = swap for live hub now; Make polish later |
| 7 | Sidecar shape = scripts until Phase 7 polish |
| 8 | Planner = one-time import into enforcer DB |
| 9 | SoftLand HTTP fallback = keep opt-in through migration; **delete in Phase 7** |
| 10 | F2 plan→SoftLand = Phase 6b after CRUD (6a) |
| 11 | Focus chrome = nav-only in Phase 0 (docks later) |
| 12 | Strict 0→1 |
| 13 | Phase 7 bar = SoftLand/Arm/Settings; Calendar may lag until Phase 6 |
| 14 | This umbrella spec written before/at execution start |
| 15 | Priority A + Phase 0 freeze |

---

## 5. Phased delivery

| Phase | Deliverable | Exit smoke |
|-------|-------------|------------|
| **0** | Study door cutover; interstitial; Open Study link; freeze | Productivity only from Focus tray |
| **1** | Live Settings hub (not Make); SoftLand/Arm/rules/schedules via gateway; enforcer-down UX | Toggles work with `:8000` killed; no HTTP mutators; pipe-down hard-blocks |
| **2** | P5a unlock in SQLite + in-flight migration + spend_free Settings | `smoke_p5a.ps1`; spend_free with Study stopped |
| **3** | Native session scoring + rollup-mirror schema + reason vocab vs `blockKindForUrl` | Overview from mirrors; reason strings match |
| **4** | Browser `track_tab` only; wearables stay sidecar; retire Python kill/track | Sessions with Study stopped; hub still receives Watch |
| **5** | Focus reads rollups/mirrors (no focus-dashboard) | Calendar loads, no `:8000` network failures for stats |
| **6a** | Planner import + gateway CRUD | Edit blocks offline |
| **6b** | Plan block → SoftLand gateway events | Coupling without second SoftLand brain |
| **7** | Stop Focus auto-start `:8000`; delete HTTP SoftLand fallback; verify matrix | Full standalone matrix |
| **3b** | Optional native LLM client | After 7; shrinks sidecar |

**Do not** parallelize Phase 2 with Phase 6.

### Phase 1 note (dependency)

`spend_free` / day-pass are **not** Phase 1 exit criteria — data becomes native in Phase 2.

### Phase 7 verify matrix (summary)

1. Kill all Python except `tracker_hub` → Arm/SoftLand/Settings including spend_free  
2. Day-pass / free window open sites; survive restart  
3. Desktop + browser sessions accumulate  
4. Calendar/Plan CRUD + rollups  
5. Study alone → interstitial; no SoftLand mutators  
6. Google/LLM only after explicit sidecar  
7. Reason strings match; then delete HTTP fallback  
8. Pipe killed → Settings hard-blocks  

---

## 6. Key code anchors

| Area | Path |
|------|------|
| Focus shell detect | `src/utils/focusDesktopShell.ts` |
| Study productivity plugin | `src/plugins/productivity_plugin.tsx` |
| Productivity page | `src/pages/ProductivityPage.tsx` |
| Live Settings panels | `FocusControlPanel`, `ProductivityPolicyPanel`, `SoftLandSiteRulesPanel`, `GateSchedulesPanel`, `AppKillRulesPanel` |
| Native bridge | `src/lib/enforcerNativeCmd.ts` |
| Focus auto-start API | `native/calt_focus/src/app.cpp` |
| Gateway / DB | `native/calt_enforcer/src/cmd_gateway.cpp`, `productivity_store.cpp` |
| SoftLand decide | `native/calt_msg_host/src/softland_decide.cpp` |
| Gate HTTP fallback | `calt-gate-extension/background.js` (`caltSoftlandHttpFallback`) |

---

## 7. Doc updates as phases land

- [AGENTS.md](../../../AGENTS.md) Current focus table  
- [future Plan/Calendar export](../exports/2026-09-08-future-plan-calendar-watch-cpp.md)  
- [BLOCKING_RULES.md](../../BLOCKING_RULES.md)  
- Per-phase implementation plans under `docs/superpowers/plans/`

---

## 8. Approval

Decisions in §4 are owner-locked. Implementation proceeds Phase 0 → … → 7 without re-opening architecture unless product lock changes.
