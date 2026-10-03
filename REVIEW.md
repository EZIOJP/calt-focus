# External review map — what to open

Reviewers who only zip **`calt-focus/`** will miss the shared React UI. That is intentional layout, not missing code.

## Where the files actually are

| Layer | Path | Notes |
|-------|------|--------|
| **Idea + architecture** | `calt-focus/README.md` | Start here |
| **C++ backend** | `calt-focus/backend/calt_enforcer/` · `calt_focus/` · `calt_msg_host/` | SoftLand SoT, kills, Focus shell, Gate native messaging |
| **Vite entry** | `calt-focus/frontend/vite.config.ts` | Builds `dist-focus/` |
| **React UI (shared)** | repo-root **`src/`** | Especially `src/components/productivity/**` |
| **Extensions** | `calt-focus/extensions/` | Gate SoftLand + SelfTracker |
| **Data** | `data/productivity/` | Not under `calt-focus/` |

### Shared React checklist (must review with Focus)

- `src/components/productivity/**` — Settings hub, SoftLand, Arm, plan cards  
- `src/lib/enforcerNativeCmd.ts` — named-pipe writes  
- `src/api/focusMirrors.ts`, `plannerClient.ts`, `bibleClient.ts`, `journalClient.ts`  
- `src/utils/focusDesktopShell.ts`, `focusDataUrl.ts`  
- Pages: `ProductivityPage.tsx`, `FocusPage.tsx`, `JournalPage.tsx`, `pages/bible/**`

See also: [frontend/README.md](./frontend/README.md).

## SoftLand connectivity (current lock)

| Path | Status |
|------|--------|
| URL decide (`get_mode`) | Gate → `com.calt.msg_host` → C++ only |
| DNR / morning cache (`get_softland_doc`) | Gate → msg_host → `softland_policy.json` (native first) |
| Study `:8000` `/api/behavior/distraction-gate` | **Opt-in only** (`storage.local.caltGateHttpFallback`) |
| Settings / Plan writes | Focus UI → `\\.\pipe\calt_enforcer_cmd` |

## Commitment / bypass notes (verified)

| Issue | Status |
|-------|--------|
| `arm.set` lock_mode-before-password bypass | **Fixed** — unlock gates on *persisted* lock credentials |
| `softland.set_enabled` off without confirm | **Fixed** — server requires `confirm: "UNLOCK"` |
| Named pipe open to any local process | Still true; sensitive ops now enforce confirm/unlock server-side |
| Default install = user Scheduled Task | Documented; prefer `install_native_enforcer.ps1` for commitment; anti-tamper kills PowerShell/WT while locked |
| Enforcer pointed at Study `vocab_app.db` | **Fixed** — console + Admin service default to `data/productivity/productivity.db` |
| Dual SoftLand decide (msg_host vs enforcer) | Intentional for SoftLand during enforcer restart — keep logic in sync by hand |
| Hand-rolled JSON `\n`/`\uXXXX` decode | **Partial** — fixed in gateway + life_content string reads |

## Focus day loop (2026-09-11)

| Slice | Status |
|-------|--------|
| Morning plan confirm native | `day.confirm_plan` + Focus `ConfirmPlanButton` |
| Day tasks + landing | `day.task_*`, `day.loop_snapshot`, `DayLoopLanding` |
| Dual-gate 1h free | `day.evaluate_close` (≥50% planned + checkboxes) |
| Carry / import prompts | FE only (no auto extend) |
| Bedtime overlay + emergency | `softland.set_bedtime`, `emergency_winddown`, `BedtimeOverlay` |

Spec: `docs/superpowers/specs/2026-09-11-focus-day-loop-design.md`

Smoke (enforcer running on productivity.db):
`gateway_cmd.ps1 -Op softland.set_enabled -PayloadFile …` → `confirm_required` without UNLOCK;
`arm.set` with `lock_mode:none` while password-armed → `unlock_failed`.

## Do not treat as Focus bugs

- Study FastAPI under repo `backend/` (Python) — Study content / legacy gate HTTP  
- Vocab / math / notes pages under `src/` — Study product  
- `run.bat` `:8000` — not required for SoftLand/Arm/Plan
