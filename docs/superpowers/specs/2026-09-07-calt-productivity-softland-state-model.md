# CALT Productivity — Phase 2 SoftLand state data model

**Date:** 2026-09-07  
**Status:** Phase 2 writers/migrator **landed** for JSON SoT era; **writers SoT superseded for next cutover** by [2026-09-08-calt-productivity-phase2-gateway-design.md](./2026-09-08-calt-productivity-phase2-gateway-design.md) (SQLite SoT + enforcer gateway; JSON becomes mirror). SoftLand decide in C++ = Prod P3 **done**; HTTP fallback removal = Prod P4 in that addendum.  
**Parent product:** [2026-09-07-calt-productivity-cpp-product-design.md](./2026-09-07-calt-productivity-cpp-product-design.md)  
**Implementation (JSON era):** `backend/behavior/softland_policy.py` · GET/PUT `/api/behavior/softland-policy` · site-rules / gate-schedules / SoftLand enable write SoT  
**Goal:** C++ Productivity owns SoftLand / day-pass / free / incubation / schedules / site rules as source of truth. Study webapp does not.

---

## 1. Store layout (v1 recommendation)

| Store | Path | Role |
|-------|------|------|
| **Hard-block hot path** | `data/behavior/enforcer_policy.json` | Keep as today — Arm, exes, lock, anti-tamper, `gate_locked`, `incubation_active` mirror for kill condition |
| **SoftLand hot path** | `data/behavior/softland_policy.json` | **New** — single JSON SoftLand engine reads every decide |
| **Optional durable** | `data/productivity.db` (or tables in existing SQLite later) | Ledgers, history, goals — Phase 2b; not required for first SoftLand parity |

**v1 rule:** SoftLand engine and Settings UI read/write **`softland_policy.json`**.  
Do not make Python `productivity_policies` / `distraction_gate` the source of truth after Phase 2 cutover.

Migration: one-shot copy from current files into `softland_policy.json`; Python may keep dual-write during transition, then become read-only / deleted for SoftLand fields.

---

## 2. `softland_policy.json` schema (v1)

```json
{
  "schema_version": 1,
  "updated_at": "2026-09-07T12:00:00+05:30",
  "softland_enabled": false,
  "site_rules": {
    "allow_extra": [],
    "watch_extra": [],
    "block_extra": []
  },
  "schedules": {
    "enabled": false,
    "windows": [
      {
        "id": "weekday-focus",
        "label": "Weekday focus",
        "days": [0, 1, 2, 3, 4],
        "start": "09:00",
        "end": "18:00",
        "mode": "study"
      }
    ]
  },
  "runtime": {
    "free_until": null,
    "free_after_hm": null,
    "incubation_until": null,
    "day_pass": {
      "date": null,
      "spent": false,
      "remaining_seconds": 0
    },
    "reward_day_active": false,
    "earned_ledger_seconds": 0
  },
  "goals": {
    "daily_focus_minutes": 0,
    "goal_met": false,
    "bible_done": false,
    "plan_confirmed_for_date": null,
    "plan_confirmed": false
  },
  "mode_flags": {
    "study": {
      "block_watch_sites": true,
      "block_porn": true,
      "block_social": true,
      "block_keywords": true,
      "block_other": true,
      "strict_allowlist": true
    },
    "free": {
      "block_watch_sites": false,
      "block_porn": true,
      "block_social": false,
      "block_keywords": true,
      "block_other": false,
      "strict_allowlist": false
    }
  },
  "builtin_lists_note": "Built-in porn/social/watch hosts stay compiled defaults in SoftLand engine code until Phase 3; user extras live in site_rules only."
}
```

### Field dictionary

See **§2.1 Field-level schema** (authoritative). Migration sources remain in §4.

**Reject separate `study_flags.json` as SoftLand SoT.**  
Those flags live **inside** `softland_policy.json` and are edited by the C++ Productivity UI.

**Stay Python/SQLite (not SoftLand decide SoT):** session scoring (`productive_categories`, `threshold`, `app_overrides`), `break_sessions` / `reward_ledger` row history (optional Phase 2b), Study content.

**Not in SoftLand JSON (stay enforcer):**

| Field | File | Notes |
|-------|------|-------|
| `hard_block_armed` | `enforcer_policy.json` | Kill switch only |
| `hard_block_exes` / kill list | `enforcer_policy.json` | OS kills |
| `lock_mode`, password/phrase/timer | `enforcer_policy.json` | Unlock challenges |
| `anti_tamper` | `enforcer_policy.json` | |
| `gate_locked` | `enforcer_policy.json` | Kill AND condition with Arm |
| `incubation_active` | `enforcer_policy.json` | **Mirror** of SoftLand `runtime.incubation_until > now` — SoftLand store is source; publish mirror on write |

---

## 2.1 Field-level schema (authoritative)

Writer codes:

| Code | Meaning |
|------|---------|
| **UI** | `calt_focus` Settings / Focus SoftLand controls |
| **Native** | SoftLand engine or `calt_msg_host` (runtime consume only, e.g. spend day-pass) |
| **Migrator** | One-shot / dual-write import from legacy files |
| **StudyEvent** | Optional later Study → productivity event (never SoftLand SoT alone) |
| **System** | Written on every successful save (`updated_at`) |

Reader: **Engine** = SoftLand decide (Phase 3+). **UI** = Settings display.

| Field | Type | Default | Writer | Read by Engine | Notes |
|-------|------|---------|--------|----------------|-------|
| `schema_version` | int | `1` | Migrator / UI | yes (compat) | Bump on breaking schema |
| `updated_at` | string (ISO-8601) \| null | `null` | System | no | Debug |
| `softland_enabled` | bool | `false` | UI, Migrator | yes | Never sets `hard_block_armed` |
| `site_rules.allow_extra` | string[] | `[]` | UI, Migrator | yes | Hostnames, lowercase |
| `site_rules.watch_extra` | string[] | `[]` | UI, Migrator | yes | |
| `site_rules.block_extra` | string[] | `[]` | UI, Migrator | yes | Merged into force-watch path |
| `schedules.enabled` | bool | `false` | UI, Migrator | yes | |
| `schedules.windows` | object[] | see example | UI, Migrator | yes | Empty → no schedule force |
| `schedules.windows[].id` | string | required | UI | yes | Stable id |
| `schedules.windows[].label` | string | `""` | UI | no | Display |
| `schedules.windows[].days` | int[] | `[]` | UI | yes | 0=Mon … 6=Sun; empty = all days |
| `schedules.windows[].start` | string | `"09:00"` | UI | yes | `HH:MM` local |
| `schedules.windows[].end` | string | `"17:00"` | UI | yes | May wrap midnight |
| `schedules.windows[].mode` | string | `"study"` | UI | yes | `study`\|`free`\|`planning`\|`bible` |
| `runtime.free_until` | string \| null | `null` | UI, Native | yes | ISO-8601; null = no PIN free |
| `runtime.free_after_hm` | string \| null | `null` | UI, Migrator | yes | `HH:MM` or null |
| `runtime.incubation_until` | string \| null | `null` | UI, Native | yes | On write, mirror `enforcer_policy.incubation_active` |
| `runtime.day_pass.date` | string \| null | `null` | UI, StudyEvent | yes | `YYYY-MM-DD` local |
| `runtime.day_pass.spent` | bool | `false` | UI, Native | yes | |
| `runtime.day_pass.remaining_seconds` | int | `0` | UI, Native | yes | ≥ 0 |
| `runtime.reward_day_active` | bool | `false` | UI, StudyEvent | yes | All-day SoftLand relief |
| `runtime.earned_ledger_seconds` | int | `0` | UI, Native | yes | Spendable balance snapshot |
| `goals.daily_focus_minutes` | int | `0` | UI, Migrator | yes | 0 = goal gate off |
| `goals.goal_met` | bool | `false` | UI, Native | yes | Productivity-owned after cutover |
| `goals.bible_done` | bool | `false` | UI, StudyEvent | yes | Morning SoftLand chain |
| `goals.plan_confirmed_for_date` | string \| null | `null` | UI, StudyEvent | yes | `YYYY-MM-DD` |
| `goals.plan_confirmed` | bool | `false` | UI, StudyEvent | yes | Meaningful with matching date |
| `mode_flags.study.block_watch_sites` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.study.block_porn` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.study.block_social` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.study.block_keywords` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.study.block_other` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.study.strict_allowlist` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.free.block_watch_sites` | bool | `false` | UI, Migrator | yes | |
| `mode_flags.free.block_porn` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.free.block_social` | bool | `false` | UI, Migrator | yes | |
| `mode_flags.free.block_keywords` | bool | `true` | UI, Migrator | yes | |
| `mode_flags.free.block_other` | bool | `false` | UI, Migrator | yes | |
| `mode_flags.free.strict_allowlist` | bool | `false` | UI, Migrator | yes | |
| `mode_flags.planning.*` / `mode_flags.bible.*` | bools | copy study defaults if omitted | UI, Migrator | yes | Optional keys; engine falls back to `study` |

**StudyEvent rule:** Study may request a field update (e.g. `bible_done=true`); Productivity app validates and writes. SoftLand never reads Study HTTP for decide.

---

## 3. Decide inputs SoftLand engine needs (Phase 3 preview)

From `softland_policy.json` + request `{ url, now }`:

| Input | Source |
|-------|--------|
| SoftLand on/off | `softland_enabled` |
| Free window | `runtime.free_until`, `runtime.free_after_hm` |
| Incubation | `runtime.incubation_until` |
| Schedule mode | `schedules` + local clock |
| Domain allow/watch/block | `site_rules` + compiled defaults |
| Mode block switches | `mode_flags.<resolved_mode>` |
| Day-pass / plan / goal gates | `runtime.day_pass`, `runtime.reward_day_active`, `goals` |

**Not required at decide time:** live Python, Study Loop content, notes DB.

### 3.1 Gate `get_mode` contract (stub — lock before P3 code)

**Transport:** Native Messaging → `calt_msg_host` → SoftLand engine.  
**HTTP `:8000`:** fallback only until native parity (see §3.2).

**Request (v1):**

```json
{
  "type": "get_mode",
  "schema_version": 1,
  "url": "https://www.youtube.com/watch?v=…",
  "tab_id": 1,
  "now": null
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `type` | string | yes | `"get_mode"` |
| `schema_version` | int | yes | `1` |
| `url` | string | yes | Full URL |
| `tab_id` | int \| null | no | Extension tab id |
| `now` | string \| null | no | ISO-8601 override for tests; null = host clock |

**Response (v1):**

```json
{
  "schema_version": 1,
  "action": "block",
  "mode": "study",
  "reason": "watch_list",
  "softland_enabled": true,
  "enforce": true,
  "interstitial": true,
  "until": null,
  "matched": "youtube.com",
  "redirect_url": null
}
```

| Field | Type | Notes |
|-------|------|-------|
| `action` | string | `allow` \| `block` \| `none` |
| `mode` | string | Resolved SoftLand mode |
| `reason` | string | Stable machine reason |
| `softland_enabled` | bool | Echo |
| `enforce` | bool | Extension should apply SoftLand |
| `interstitial` | bool | Show SoftLand page vs hard DNR only |
| `until` | string \| null | When current free/incubation ends |
| `matched` | string \| null | Host/rule that matched |
| `redirect_url` | string \| null | Optional morning/plan URL |

Engine must answer without calling Python.

### 3.2 Migration: HTTP fallback until native parity

| Stage | SoftLand decide path |
|-------|----------------------|
| Today / dual-read | Gate → HTTP; Python SoftLand reads `softland_policy.json` |
| **P3 (current)** | Gate → msg-host → C++ `get_mode` first; HTTP fallback if host missing |
| P4 | Fallback deleted; uvicorn optional for Study only |

Force HTTP-only in Gate: storage `caltSoftlandHttpFallback=true`.

---

## 4. Migration map (current → Phase 2)

| Current location | Fields | Target |
|------------------|--------|--------|
| `data/behavior/softland_site_rules.json` | `allow_extra`, `watch_extra`, `block_extra` | `site_rules.*` |
| `data/behavior/gate_schedules.json` | `enabled`, `windows[]` | `schedules.*` |
| SQLite `productivity_policies` SoftLand column | `hard_block_enabled` → `softland_enabled` | `softland_enabled` |
| `#policy` SoftLand toggles / day-pass UI | various | write `softland_policy.json` via Focus shell (C++ or API that writes file only) |
| Free override (Python) | until timestamp | `runtime.free_until` |
| Incubation (`break_reward` / gate publish) | active flag | `runtime.incubation_until` + mirror `enforcer_policy.incubation_active` |
| Earned spend ledger | minutes/seconds | `runtime.earned_ledger_seconds` |
| Built-in porn/social/watch lists in `browser_gate_policy.py` | compiled tuples | Stay in SoftLand **engine code** v1; user extras only in JSON |
| `#policy` `hard_block_exes` game-bank | SoftLand labeling | Optional later; **not** OS kill list — do not confuse with `enforcer_policy` exes |

---

## 5. Writers and readers (Phase 2 cutover)

| Actor | May write | May read |
|-------|-----------|----------|
| `calt_focus` Settings `#rules` / `#policy` / `#focus` SoftLand controls | `softland_policy.json` (and enforcer JSON for Arm only) | both |
| SoftLand engine / `calt_msg_host` | runtime clocks only if consuming day-pass (optional) | `softland_policy.json` |
| `calt_enforcer` | status JSON; optional mirror fields | `enforcer_policy.json` |
| Study webapp | **Forbidden** as SoftLand source of truth after cutover; optional `study_events` table later | optional analytics |

During dual-write: Settings may call a thin Python “write file” helper **or** Focus may write the JSON via native bridge — prefer native write so Python need not run.

---

## 6. Explicit non-goals for Phase 2

- Porting `distraction_gate.py` / `browser_gate_policy.py` evaluate into C++ (that is **Prod P3**)
- Wiring Gate `connectNative` (P3)
- Moving notes / GRE / Bible **content** tables
- Replacing `enforcer_policy.json` kill schema
- Full `productivity.db` ledger history UI (Phase 2b / P6)

---

## 7. Exit criteria (Phase 2 done)

1. `softland_policy.json` exists with schema_version 1 and fields above. **Met** (migrator + defaults).  
2. Settings SoftLand site rules, schedules, SoftLand enable, free/day-pass/incubation controls persist **there** (not SQLite SoftLand column as SoT). **Met** for site rules / schedules / softland_enabled (enable dual-writes SQLite + SoT; SoT wins on read). Runtime day-pass/free/incubation UI can PATCH `/api/behavior/softland-policy`.  
3. Documented one-shot migrator from `softland_site_rules.json` + `gate_schedules.json` + SoftLand policy flag. **Met** (`migrate_from_legacy` + `POST …/softland-policy/migrate`).  
4. SoftLand evaluate still Python **or** stub — allowed until P3 — but **reads this store** (or dual-read with C++ store winning). **Met** (`distraction_gate` dual-reads `softland_enabled`; site lists via SoT).  
5. No SoftLand ON path that Arms `hard_block_armed`. **Met** (save strips arm; incubation mirror preserve_lock only).

---

## 8. Decision log

| Date | Decision |
|------|----------|
| 2026-09-07 | Phase 2 SoT = `data/behavior/softland_policy.json` v1 |
| 2026-09-07 | Keep `enforcer_policy.json` for kills; mirror `incubation_active` only |
| 2026-09-07 | Built-in block lists stay in engine code until later; user extras in JSON |
| 2026-09-07 | No SoftLand evaluate C++ until this model approved + Phase 2 writers land |
| 2026-09-07 | Inventory merge: add `free_after_hm`, `reward_day_active`, `goal_met`, `bible_done`, `mode_flags`; reject P5a `study_flags.json` as SoT |
| 2026-09-07 | Field-level schema table (type/default/writer/reader) + `get_mode` stub + HTTP fallback stages |
| 2026-09-07 | Phase 2 writers/migrator implemented (`softland_policy.py`, dual-write legacy, dual-read Gate enable) |

**Owner next:** Prod P3 native SoftLand decide — only when explicitly opened.  
**Not open:** Gate `connectNative` / remove HTTP SoftLand.
