# CALT Focus FE flows — order check (React)

**SoT UI:** React SPA → `dist-focus/` (Study `src/` build). **HTML shell deleted** (`a6b90b2`, 4 Oct 2026). Do not use `frontend/shell`.

**Counts (React, 9 Oct 2026):** Live **18** · Partial **1** · Backend only **4** · Stub **0**

Partials left: Plan LLM “propose week” (Study `:8000` — intentionally disabled in Focus; use routines / calendar / work sessions).

Ship FE: `npm run build:focus` (builds Study + syncs `dist-focus/`). Tray → Reload UI / restart `calt_focus`.

---

## Happy-path order (must stay true)

```text
New day
  → SoftLand host gate ON, Arm OFF
  → Bible pending → DayGateStrip (Plan/Calendar still editable; SoftLand blocks hosts)
  → If planning_enabled:
        Confirm couples SoftLand + Arm (strip CTA → Plan / Home Confirm)
     Else:
        DayLoop auto-confirms after Bible (no Confirm phase)
  → SoftLand site/schedule/mode editable after morning clear (or free day)
  → Arm / kill-list still need free day or day pass
  → Prefer Home work session (Allow + Start) for focus blocks
```

**Locks:** SoftLand ON ≠ Arm. Confirm (or auto-confirm when planning off) is the couple point. **No plan carry-forward** (`carry_disabled`).

**Session-first (current default):** `goals.planning_enabled` defaults **false** (FE + msg-host + enforcer agree). Work sessions and routines/plan edits are **not** free-day gated and are **not** trapped behind full-screen morning modals.

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
| Morning bible gate | Until `bible_done_for_date == today` | `MorningBibleOverlay.tsx` | `bible.devotion.done` | **Live** | Overlay when pending; hides after mark done |
| Morning plan gate (hosts) | SoftLand on + bible/plan incomplete → `morning_*` modes | SoftLand decide + overlays | SoftLand `get_mode` | Backend only | Non-allow sites blocked until bible + Confirm (or auto if planning off) |
| Confirm plan → SoftLand + Arm | Bible + (≥1 non-free min if planning on) | `ConfirmPlanButton` / `MorningPlanOverlay` | `day.confirm_plan` | **Live** | Empty plan → `plan_required` when planning on; Arm off until Confirm/auto |
| Planning on/off | Skip Confirm phase | Settings → More → Planning | `softland.patch_goals` `planning_enabled` | **Live** | Off: no plan overlay; day opens after Bible |
| Plan follow (active block) | SoftLand follow only when confirmed today | — | `ApplyActivePlanToSoftland` / tick | Backend only | Mode tracks current block after Confirm |
| No plan carry-forward | Import / roll → `carry_disabled` | Calendar/Plan (ops rejected) | `plan.import_from_date` / `roll_forward` | **Live** (enforced) | Cannot import yesterday into today |
| Work session | Allow apps/sites → Start / End | `HomeAllowBlockRail.tsx` | `session.set` | **Live** | Start any day; Allow anytime; Block·Arm needs free day |
| Goal free (productive min) | Tracked ≥ daily_focus → `free_until` EOD; drop study-temp kills | Home progress line + FREE badge | `DayLoopMaybeGrantGoalFree` | **Live** | After goal: SoftLand free; games/social Arm stay |
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
| LLM host / Jarvis | OpenRouter / 9Router Suggest + brief | `FocusLlmHostPanel`, `FocusJarvisPanel` | localStorage + `/v1/chat` | **Live** | Settings → More; Home AI chip |
| Jarvis commands | In-app command box (status, report, softland…) | `focusCommands` + Jarvis panel | enforcer / mirrors | **Live** | Home Jarvis card; type `help` |
| WhatsApp daily report | Numbers + send time → wa.me report | `FocusWhatsAppReportPanel` | localStorage + mirrors | **Live** | Settings → More; tap Send in WhatsApp |
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
