# CALT Focus FE flows — order check (React)

**SoT UI:** React SPA → `dist-focus/` (Study `src/` build). **HTML shell deleted** (`a6b90b2`, 4 Oct 2026). Do not use `frontend/shell`.

**Offline Math→ML:** `frontend/jp-roadmap/` (multi-file zip layout) → WebView `https://calt-learn.app/` (tray **Open Math → ML**). No CDN/Firebase; SoftLand always-allows `calt-learn.app`. Progress SoT: `productivity.db` (`productivity_roadmap_state` + `productivity_roadmap_attempts`) via enforcer `roadmap.*`; mirror `behavior/jp_roadmap.json`. Check: `python scripts/run/check_roadmap_db.py`.

**Counts (React, 9 Oct 2026):** Live **18** · Partial **1** · Backend only **4** · Stub **0**

Partials left: Plan LLM “propose week” (Study `:8000` — intentionally disabled in Focus; use routines / calendar / work sessions).

Ship FE: `npm run build:focus` (builds Study + syncs `dist-focus/`). Tray → Reload UI / restart `calt_focus`.

---

## Happy-path order (must stay true)

```text
New day
  → SoftLand host gate ON, Arm OFF
  → Bible pending → Focus stays on /bible (morning; afternoon from 12:00; evening from 17:00)
  → Planning is required: Confirm (≥1 planned minute) couples SoftLand + Arm
        Until Confirm, Focus stays on the plan. An empty confirm is reopened.
  → SoftLand site/schedule/mode editable after morning clear (or free day)
  → Arm / kill-list still need free day or day pass
  → Prefer Home work session (Allow + Start) for focus blocks
```

**Locks:** SoftLand ON ≠ Arm. Confirm is the couple point. Planning cannot be turned off. **No plan carry-forward** (`carry_disabled`).

**Day open:** morning Bible, then a finished plan, then the day runs normally. Afternoon Bible is required from 12:00 and evening Bible from 17:00; until that slot is marked, Focus stays on `/bible` and non-allow sites stay blocked. Routine and plan edits stay open until Confirm, and on a free day or day pass. After Confirm they return `free_day_required`.

---

## What you can click-test today

| Area | Where | Ready? |
|------|--------|--------|
| Morning Bible mark-done | Overlay → Open Bible / Mark done | Live |
| Confirm plan | Overlay or Home CTA (when planning on) | Live |
| Home glance + work session | Home tab | Live |
| Day todos + Check unlock | Home checklist / DayLoopLanding | Live |
| Calendar CRUD / DnD | Calendar tab | Live |
| Plan / routines / sleep | Plan tab + Routines | Live |
| Settings SoftLand + Arm | Settings (MakeSettingsApp) | Live |
| Reward / day pass | Settings → Unlock | Live |
| Bible reader | `/bible` | Live |
| Journal | `/journal` | Live (manual Save) |
| Device lock | Settings → Blocks / device panel | Live |
| Bedtime + Emergency | `BedtimeOverlay` (type EMERGENCY) | Live |

---

## Day-loop flows

| Flow | Steps | React surface | Gateway / engine | Status | How to verify |
|------|-------|---------------|------------------|--------|---------------|
| New day reset | Clears sticky bible/plan/goal + stale `free_until` | — | `DayLoopTick` | Backend only | After midnight: flags not sticky from yesterday |
| Morning bible gate | Morning always; afternoon from 12:00; evening from 17:00 | `MorningBibleOverlay.tsx` | `bible.devotion.done` | **Live** | Shell stays on `/bible` until the due slot is marked |
| Morning plan gate (hosts) | SoftLand on + due bible or plan incomplete | SoftLand decide + overlays | SoftLand `get_mode` | Backend only | Non-allow sites blocked until due Bible + Confirm |
| Confirm plan → SoftLand + Arm | Due Bible + ≥1 non-free min | `ConfirmPlanButton` / `MorningPlanOverlay` | `day.confirm_plan` | **Live** | Empty plan → `plan_required`; Arm off until Confirm |
| Planning | Always on | Settings → Planning | `planning_enabled` forced true | **Live** | Checkbox stays checked |
| Plan follow (active block) | SoftLand follow only when confirmed today | — | `ApplyActivePlanToSoftland` / tick | Backend only | Mode tracks current block after Confirm |
| No plan carry-forward | Import / roll → `carry_disabled` | Calendar/Plan (ops rejected) | `plan.import_from_date` / `roll_forward` | **Live** (enforced) | Cannot import yesterday into today |
| Work session | Allow apps/sites → Start / End | `HomeAllowBlockRail.tsx` | `session.set` | **Live** | Start any day; Allow anytime; Block·Arm needs free day |
| Goal free (productive min) | Tracked ≥ daily_focus → `free_until` EOD; drop study-temp kills; pause Arm | Home progress line + FREE badge | `DayLoopMaybeGrantGoalFree` | **Live** | After goal: sites open except porn; games and kill-list apps can run |
| Dual-gate close free (1h) | Tasks done + ≥50% planned non-free → +60m once | Home **Check unlock** / `DayLoopLanding` | `day.evaluate_close` | **Live** | Grants once per day when gates met |
| Reward / day pass | Bible required; clears study-temp kills | Settings → Unlock (`ProductivityPolicyPanel`) | `reward.claim` / `day.grant_pass` | **Live** | Without bible → `bible_required` |
| Bedtime | `set_bedtime` → `bedtime_active` → overlay | `DayRhythmPanel` + `BedtimeOverlay` | `softland.set_bedtime` | **Live** | Bedtime suppresses morning overlays |
| Emergency winddown | Confirm `EMERGENCY` → web minus distraction; Arm keeps games | `BedtimeOverlay` | `softland.emergency_winddown` | **Live** | Typed EMERGENCY unlocks web |

---

## Screen / settings flows

| Flow | Steps | React surface | Gateway / engine | Status | How to verify |
|------|-------|---------------|------------------|--------|---------------|
| Home glance | Productive / bible / plan + Confirm + session | `HomeModeBoard`, `GlanceBar`, `focusStatusBus` | `day.loop_snapshot` + mirrors | **Live** | Numbers match `day_rollup` + SoftLand goals |
| Calendar CRUD / DnD | Day/week/month | `PlannerCalendar.tsx` | `plan.list/upsert/delete` | **Live** | Create / drag / delete round-trip to `plan_blocks.json` |
| Plan tab | Routines, range, goals blocks | `ProductivityPage` tab=plan | `routine.*` / `plan.*` | **Live** | LLM propose week disabled in Focus (Study-only) |
| Bible reader | Corpus `/calt-bible/` + devotion | `BibleReaderPage.tsx` | `bible.devotion.*` | **Live** | Mark done syncs morning gate |
| Journal | Today entry + autosave | `JournalPage.tsx` | `journal.upsert` / `summary` / `log` | **Live** | Debounce 1.5s + blur / hide |
| Settings SoftLand lists | allow/watch/block + enable | `SoftLandSiteRulesPanel`, Settings | `softland.patch_*` / `set_enabled` | **Live** | Lists need free day; UNLOCK to turn SoftLand off |
| Settings Arm | armed, exes, lock | `FocusControlPanel` / Apps | `arm.set` | **Live** | Free-day gated |
| Device lock (hosts) | DEVICE LOCK confirm | `DeviceBlockPanel.tsx` | `device_block.*` | **Live** | Separate from SoftLand; free-day gated |
| LLM host / Jarvis | Local Qwen (Ollama) Suggest + free chat | `FocusLlmHostPanel`, `FocusJarvisPanel` | localStorage + `/v1/chat` | **Live** | Settings → More; Home AI chip |
| Jarvis commands | In-app command box (status, report, softland…) | `focusCommands` + Jarvis panel | enforcer / mirrors | **Live** | Home Jarvis card; type `help` |
| Email daily report | Fixed inbox + send time → mailto report | `FocusEmailReportPanel` | localStorage + mirrors | **Live** | Settings → More; tap Send in mail app |
| Quit / watchdog | Quit refused while SoftLand or Arm on | Tray + Settings → More → Quit | `calt_focus` Quit + `focus_watchdog` | **Live** (UI) + Backend | SoftLand/Arm block Quit; helper shows why |

---

## Where to change UI

| Layer | Path |
|-------|------|
| React SoT | Study `src/components/productivity/*`, `src/pages/*`, `calt-focus/frontend/app/FocusApp.tsx` |
| Ship | `npm run build:focus` → `dist-focus/` |
| Rules / mirrors | `docs/BLOCKING_RULES.md`, `docs/SETTINGS.md`, `data/productivity/behavior/*` |
| Enforcer | `backend/calt_enforcer/src/{cmd_gateway,day_loop,work_session}.cpp` |

Legacy checklist prompt (shell era): [`docs/cursor-prompts/complete-shell-checklist.md`](cursor-prompts/complete-shell-checklist.md) — treat Q0 as answered: **React only**.
