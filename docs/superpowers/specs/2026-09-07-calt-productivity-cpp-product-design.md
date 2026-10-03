# CALT Productivity (C++) — product + P5 design

**Date:** 2026-09-07  
**Status:** Owner-approved direction — design lock before code  
**Replaces:** “SoftLand forever on Python :8000” for blocker decisions  
**Does not replace:** Study content webapp (notes, GRE, Math, Journal, Bible pages, Study Loop UI)

**Supersedes for SoftLand-as-brain:** [2026-09-07-calt-blocker-study-split-decisions.md](./2026-09-07-calt-blocker-study-split-decisions.md) D1 (SoftLand compute stays Python) and parked P5/P6 ranking.  
**Phase 2 data model:** [2026-09-07-calt-productivity-softland-state-model.md](./2026-09-07-calt-productivity-softland-state-model.md)

---

## 1. Product split (locked)

| Product | Tech | Owns |
|---------|------|------|
| **CALT Productivity** | C++ (`calt_enforcer` + SoftLand engine + tracker + msg-host + Focus shell) | Tracking, SoftLand, hard-block, schedules, day-pass, goals-as-blocker-state, productivity settings, tray |
| **CALT Study** | Python FastAPI + React | Notes, Study Loop *content*, GRE, Math, Journal, Bible *content*, pure study workflows |

**Singular blocker / productivity engine: yes (C++).**  
**Singular whole CALT monolith: no.**

Python is **not** required for:
- SoftLand allow/block
- OS kills
- session tracking runtime
- Arm / lock / anti-tamper
- day-pass / free window accounting

Python **may** optionally emit study events into the productivity store later. It is not the SoftLand brain.

---

## 2. Naming

| Name | Role |
|------|------|
| `calt_enforcer.exe` | Kills, lock, anti-tamper, non-browser (+ expanding) sessions, stay-alive |
| SoftLand engine (in host and/or enforcer process) | Site rules, schedules, mode, day-pass, free/incubation windows |
| `calt_msg_host.exe` | Native Messaging for Gate; asks SoftLand engine; no Python round-trip for mode |
| `calt_focus.exe` | Tray + WebView2 (or growing native UI) → productivity Settings / Focus |
| Study webapp | Separate product on `:8000` for content only |

Working product name: **CALT Productivity**.  
Study site stays **CALT Study**.

**Naming (forever):** `softland_enabled` = sites / SoftLand only. `hard_block_armed` = native kill switch only. SoftLand ON ≠ Arm.

---

## 3. Architecture

```text
┌──────────────────────────────────────────────────────────┐
│ CALT Productivity (C++)                                  │
│                                                          │
│  calt_focus.exe     tray + UI (Settings / Focus)         │
│         │                                                │
│         ▼                                                │
│  policy / state store (SQLite and/or JSON under data/)   │
│         │                                                │
│  ┌──────┴───────┐      ┌─────────────────────────────┐   │
│  │ SoftLand     │      │ calt_enforcer               │   │
│  │ engine       │      │ kills · lock · sessions     │   │
│  └──────┬───────┘      └──────────────▲──────────────┘   │
│         │                             │                  │
│  calt_msg_host ◄── Gate (Edge)        │ status/policy    │
│  (stdio native messaging)             │                  │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ CALT Study (Python + React) — optional companion         │
│  notes · Study Loop UI · GRE · Math · Journal · Bible    │
│  may WRITE study_events into productivity store (later)  │
│  does NOT answer SoftLand mode · does NOT kill           │
└──────────────────────────────────────────────────────────┘
```

**SoftLand ON ≠ Arm.**  
SoftLand never sets `hard_block_armed`. Arm never SoftLands sites by itself.

---

## 4. State owned by C++ Productivity

### 4.1 Hard-block (already mostly there)

- `hard_block_armed`
- kill list (exes)
- lock mode (timer / password / phrase)
- anti-tamper targets
- `enforcer_status.json` / ownership lock

### 4.2 SoftLand (moves to C++)

- `softland_enabled`
- site rules (allow / block / patterns)
- schedules (windows when SoftLand applies)
- mode output: STUDY / FREE / blocked interstitial / incubation, etc.
- day-pass remaining / spent
- free_until / spend-earned windows
- incubation_until
- simple productivity goals that affect SoftLand (if any)

### 4.3 Tracking

- non-browser FG sessions (done in enforcer)
- browser tab sessions: target = native path over time (Gate/SelfTracker may write via host or shared DB; Python ingest not required for blocker)

### 4.4 Shared store

Prefer **one SQLite** under productivity control (can be same file as today with clear tables, or `data/productivity.db`).  
JSON files allowed for policy hot path (`enforcer_policy.json`, `softland_policy.json`) if simpler for v1.

**Study webapp does not own these tables as source of truth.**

---

## 5. SoftLand decide (C++)

Gate calls native only:

```text
In:  { type: "get_mode", url, tab_id, ... }
Out: { mode, reason, interstitial?, until?, ... }  // same UX outcomes as today
```

Algorithm (ordered, v1 — refine in implementation plan):

1. If SoftLand disabled → allow (site path).  
2. If active free window / spent earned → allow (within rules).  
3. If incubation active → apply incubation policy.  
4. If outside schedule window → allow or policy default.  
5. Match URL/domain against site rules.  
6. Apply day-pass / goal gates if configured.  
7. Else block / interstitial / study-only per rule.

**No live HTTP to `:8000` for this decision.**

Migration: Gate keeps HTTP fallback flag until native parity is trusted; then remove.

---

## 6. Study coupling (under this product)

| Model | Rule |
|-------|------|
| **Primary** | Day-pass, free windows, goals that affect blocking are **edited and stored in Productivity (C++)** |
| **Optional later** | Study webapp emits events (`bible_completed`, `quiz_finished`) into productivity store; C++ may treat them as inputs |
| **Forbidden** | SoftLand mode endpoint remaining on Python as the live brain |

Plan confirmed / goal progress / day-pass spent are **productivity features**, not side effects of the study site.

---

## 7. UI

- **Daily door:** `calt_focus.exe` (tray).  
- **Settings hub:** existing React Settings/Focus **or** gradually more native — still loaded by the C++ shell.  
- **Study content:** browser → study webapp routes only when user wants notes/quizzes.  
- Tray: Open Focus / Open Productivity Settings / Quit (and later optional Open Study).

Closing the study webapp must not stop kills or SoftLand.

---

## 8. Phased delivery (no SoftLand-in-C++ code until Phase 2 store shape accepted)

| Phase | Deliverable | Exit criteria |
|-------|-------------|----------------|
| **P0** | This doc locked | Owner OK |
| **P1** | OS maturity: broad browsers, anti-tamper, Admin service | Hard-block feels solid |
| **P2** | Productivity-owned SoftLand **state** (rules, schedules, day-pass, free windows) + UI writes native store | Python policy no longer source of truth for those fields |
| **P3** | `calt_msg_host` + Gate → native SoftLand decide | **Done** — C++ `get_mode`; Gate native first; HTTP fallback until P4 |
| **P4** | Remove SoftLand mode dependency on `:8000`; HTTP fallback deleted | Singular blocker engine live |
| **P5** | Tracker completeness (browser sessions path without Python runtime) | Tracking usable offline from study API |
| **P6** | Goals / plan-confirm UX inside Productivity | Study app optional for blocker |

P5 in older maps ≈ SoftLand native decide ≈ **P3** here.  
Full “Python only study content” ≈ end of **P4–P6**.

---

## 9. Explicit non-goals

- Porting notes / GRE / Journal / Bible **content** into C++  
- Qt / PySide6 revival  
- Python kill path (P7 rejected)  
- One process for study UI + blocker (not required)  
- Cloning every Cold Turkey feature in phase 1–3  

---

## 10. Success picture

- User runs **CALT Productivity** (tray + enforcer service).  
- SoftLand and kills work with **study webapp closed** and **uvicorn stopped**.  
- Study webapp is opened only for learning content.  
- Gate never needs `:8000` for allow/block.  
- Tracking and productivity controls live in the C++ product.

---

## 11. Decision log

| Date | Decision |
|------|----------|
| 2026-09-06 | Native kills; zero Python at kill time |
| 2026-09-07 | Settings hub; calt_focus shell |
| 2026-09-07 | **Productivity + tracking + blocking = C++ product; study content = Python webapp** |
| 2026-09-07 | SoftLand decide + day-pass/free/goal blocker state owned by C++; Python not SoftLand brain |

---

## 12. Next action

1. This file is the product lock (P0).  
2. `AGENTS.md` updated with the product split.  
3. **Do not implement SoftLand-in-C++ until Phase 2 store shape is specified** — see [softland-state-model](./2026-09-07-calt-productivity-softland-state-model.md).  
4. Optional immediate code: **P1 only** (browsers + anti-tamper) — largely Done; does not conflict with this design.

**Owner:** this design is the direction.  
Next design slice: **Phase 2 data model** (exact SoftLand + day-pass fields in the C++-owned store).
