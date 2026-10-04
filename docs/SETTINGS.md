# CALT Focus — Settings tab (full reference)

How every Settings control is **presented**, which **frontend** owns it, and which **backend / enforcer** path persists or enforces it.

| Layer | Path |
|-------|------|
| Settings shell | Study sibling `src/components/productivity/settings/make/MakeSettingsApp.tsx` |
| Tab wrapper | `…/ProductivitySettingsTab.tsx` |
| Entry | `…/pages/ProductivityPage.tsx` → `?tab=settings&section=…` |
| Enforcer | `calt-focus/backend/calt_enforcer/src/` (`cmd_gateway.cpp`, SoftLand, device block, policy) |
| Focus shell | `calt-focus/backend/calt_focus/src/` (pipe bridge, WebView) |
| SoT | `data/productivity/productivity.db` + `data/productivity/behavior/*.json` mirrors |
| UX mock | `Downloads/CALT Focus Settings.html` (IA + copy reference; Productive tab stub in mock only) |

**Locks (product):** SoftLand ON ≠ Arm. SoftLand = browser sites. Arm = OS process kills. Device lock = Windows hosts (independent).

**UX naming:** Settings UI says **Site block** for SoftLand (browser). Internals, DB keys, and ops stay `softland_*`.

---

## 1. How Settings is presented

### 1.1 Chrome

- Full-height panel (`min-h-[70vh]`) inside Productivity.
- **Header:** title “Settings”, one-line reminder (`Site block and Arm are separate switches…`).
- In Focus desktop shell: **LiveStatusStrip** under the header (Site block ON/OFF · mode · Arm · enforcer · today minutes).
- **Section nav** (pill row): Overview · Blocks · Unlock · Productive · More.
- Write sections (Blocks / Unlock / Productive) wrap body in **`EnforcerWriteGate`** — if enforcer/bridge is down, UI goes dim + non-interactive with a banner.

### 1.2 URL / deep links

| Query / hash | Resolves to |
|--------------|-------------|
| `section=overview` | Overview |
| `section=blocks` | Blocks → Sites |
| `section=sites` / `apps` / `filters` | Blocks sub-tab |
| `section=unlock` (also legacy `now`, `focus`, `enforcer`) | Unlock |
| `section=productive` (also `scoring`, `classification`) | Productive |
| `section=more` / `tools` (also `export`, `demo`, `watch`, …) | More |

Parser: `parseSettingsSection()` in `MakeSettingsApp.tsx`.

### 1.3 Information architecture (CT-like pattern, no CT code)

| Nav | Meaning |
|-----|---------|
| **Overview** | What’s on + day flow + jump cards |
| **Blocks** | Edit what gets blocked (Sites / Apps / Filters) |
| **Unlock** | Goal, reward/pass, Arm (Site block lists stay under Blocks) |
| **Productive** | What minutes count + classification extras |
| **More** | Look (sticker), perf log, planning, export, setup |

---

## 2. Shared plumbing

### 2.1 Writes (Focus)

```
FE enforcerNativeCmd(op, payload)
  → calt_focus webview postMessage { type: "enforcer_cmd", … }
  → named pipe \\.\pipe\calt_enforcer_cmd
  → cmd_gateway.cpp::HandleOp / GatewayApplyOp
  → SQLite + Publish* mirrors
```

Client: Study `src/lib/enforcerNativeCmd.ts`.

### 2.2 Reads (Focus)

Mirrors under `data/productivity/behavior/` via `calt-data` / `focusDataUrl` (e.g. `softland_policy.json`, `enforcer_status.json`, `enforcer_policy.json`, `device_block.json`, `day_rollup.json`, `day_loop.json`, `plan_blocks.json`).

Live aggregation: `focusStatusBus` + `useFocusStatus`.

### 2.3 SoftLand document (SQLite)

Table `productivity_softland` (`document_json`): `softland_enabled`, `site_rules`, `schedules`, `mode_flags`, `goals`, `runtime`, reward/day-pass fields, plan flags. Mirrored to `softland_policy.json`.

### 2.4 Arm policy

`behavior/enforcer_policy.json` (`hard_block_armed`, `exes`, lock fields, `anti_tamper`, …). Loaded/synced in `policy_db.cpp`.

### 2.5 Confirm phrases (server-enforced)

| Phrase | Used for |
|--------|----------|
| `UNLOCK` | SoftLand off |
| `REWARD` | Claim reward day |
| `PASS` | Spend day pass |
| `DEVICE LOCK` | Device hosts apply/remove |

---

## 3. Overview

**FE:** `MakeSettingsApp.tsx` → `OverviewSection`, `LiveStatusStrip`, `WeaponCallout` (`settings/SettingPrimitives.tsx`).

| Control | Presentation | FE | Backend / data | Class |
|---------|--------------|----|----------------|-------|
| Site block chip | Mono pill ON/OFF (SoftLand) | `LiveStatusStrip` | `softland_policy.json` / status bus | Display |
| Mode chip | `off` / `study` / `free` / `study (incubation)` | `softlandModeLabel` | SoftLand runtime free/incubation timestamps | Display |
| Arm chip | armed / disarmed | status | `enforcer_status.json` | Display |
| Enforcer chip | up / down / no bridge | `useEnforcerReachability` | Pipe ping / bridge | Display |
| Today Xm / Ym | Productive vs goal | day rollup + goals | `day_rollup.json`, SoftLand goals | Display |
| “SoftLand ON ≠ Armed” | Caption | static | — | Copy |
| WeaponCallout | Card: SoftLand vs Arm | `WeaponCallout` | — | Copy |
| Day flow | Numbered list Bible → Confirm → goal | static | — | Copy |
| Jump buttons | Grid to Blocks / Unlock / Productive / More | `onJump` | — | Nav |

**Note:** Mode label does **not** fully re-evaluate schedule windows / morning ritual; it uses SoftLand enabled + free/incubation clocks.

---

## 4. Blocks

**FE:** `BlocksSection` with sub-tabs Sites · Apps · Filters.

### 4.1 Sites (SoftLand)

#### SoftLand master — `ProductivityPolicyPanel` `variant="softland"`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| SoftLand on | Checkbox; OFF prompts type `UNLOCK` | `saveProductivityPolicy` → `softland.set_enabled` | `cmd_gateway.cpp` SoftLand enable; `PublishSoftlandMirror` | SoftLand master |
| Save | Button | same + related policy save | SoftLand document | SoftLand |

#### Site lists — `SoftLandSiteRulesPanel`

Copy: *Allow always wins. Watch is study-only. Block survives reward days.*

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Allow / Watch / Block chips | Click chip to remove | Local until Save | — | SoftLand |
| Domain input + Add | Enter adds; one tag per domain | Local | — | SoftLand |
| Save | Button | `softland.patch_site_rules` | Gateway patches `site_rules.{allow,watch,block}_extra`; decide in `calt_msg_host` SoftLand decide | SoftLand |

Built-in watch/porn lists are **not** edited here.

#### Mode flags — `SoftLandModeFlagsPanel`

| Control | Presentation | Persist | Enforce | Class |
|---------|--------------|---------|---------|-------|
| Block watch sites (Study / Free) | Live toggle | `softland.patch_mode_flags` → `mode_flags.*.block_watch_sites` | SoftLand decide `FlagsForMode` | SoftLand **live** |
| SoftLand porn heuristic | Live toggle | `mode_flags.*.block_porn` | `LooksPorn` | SoftLand **live** |
| Block social / keywords / other / Strict allowlist | **Hidden** (not in UI) | May still exist in JSON | **Not enforced** (inert) | Dead UX |
| Save mode flags | Button | `softland.patch_mode_flags` | Gateway | SoftLand |

#### Schedule — `GateSchedulesPanel`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Enable recurring schedules | Checkbox | `softland.patch_schedules` | SoftLand decide `ResolveScheduleMode` | SoftLand |
| Windows | Label, start/end, mode (Study/Free/Planning/Bible), Mon–Sun, delete | `schedules.windows` | First matching window wins | SoftLand |
| Add window / Save | Buttons | same | Gateway | SoftLand |

### 4.2 Apps (Arm) — `AppKillRulesPanel`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Gaming / Social presets | Buttons merge exe lists | Local until Save | — | Arm |
| Exe chips | Click to remove | Local | — | Arm |
| Add exe | Input (+ `.exe`) | Local | — | Arm |
| Save | Button | `arm.set` / `putEnforcerPolicy` | `enforcer_policy.json` + runtime SQLite; kill loop in `win_service.cpp` | Arm |

SoftLand site domains never belong on this list.

### 4.3 Filters (Device) — `DeviceBlockPanel`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Enable device lock | Confirm `DEVICE LOCK` | `device_block.save` + apply | `device_block.cpp` → hosts | Device |
| Apply as Admin | Bat / elevation | `run_repo_bat` / CLI | Hosts apply | Device |
| Refresh list | Button | `device_block.refresh_list` | Scrape cache | Device |
| Status | Button / badge | Mirror / `device_block.status` | Settings + hosts state | Device |
| Remove | Confirm `DEVICE LOCK` | `device_block.remove` | Strip hosts markers | Device |
| YouTube/streaming / Social checkboxes | Detail toggles | `device_block.save` fields | Patch settings | Device |

Independent of SoftLand schedules. SoftLand Allow cannot override hosts.

---

## 5. Unlock

### 5.1 Goal & free days — `ProductivityPolicyPanel` `variant="unlock"`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Daily goal minutes | Number (≈15–960) | `softland.patch_goals` (+ local goals key) | SoftLand `goals.daily_focus_minutes` | Goals |
| Reward day | Button → type `REWARD` | `reward.claim` / bible client | Needs Bible done + reward available; sets free until EOD; clears study temp kills | Unlock |
| Day pass | Button → type `PASS` | `day.grant_pass` | Quota (2/week); Bible done; free until EOD | Unlock |
| Gate / progress | Ring / status | Read mirrors | day rollup / SoftLand | Display |
| Study Loop gate | Checkbox | Study `:8000` | — | **Study-only** (hidden in Focus) |
| Auto-block Gaming / hard_block_exes chips | Shown in some variants | **Not the Focus write path for Arm list** | Use Blocks → Apps | Prefer Apps tab |

### 5.2 Now — SoftLand & Arm — `FocusControlPanel`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Refresh | Button (+ poll) | status bus / dashboard fetch | Mirrors | Display |
| Restart enforcer | Button | `focus_shell` `restart_enforcer` | `calt_focus` shell | Shell |
| Status cards | SoftLand / blocked / incubation / free | Read-only | SoftLand + status | Display |
| Spend earned… | Minutes prompt | `softland.spend_free` | Ledger → `free_until` | Unlock |
| Free time (PIN) | Button | Study API | Disabled in Focus (`can_pin_free` false) | Study-only |
| Lock mode | None / Timer / Password / Phrase | `arm.set` | Strong lock rules on disarm | Arm |
| Lock minutes / secret | Inputs | `arm.set` | Policy JSON (secrets plaintext today) | Arm |
| Anti-tamper | Checkbox | `arm.set` | Extra kill list when locked | Arm |
| Protect uninstall | Checkbox | Written on `arm.set` | **Little/no consumer** | Mostly dead |
| Arm / Disarm | Buttons | `arm.set` `hard_block_armed` | Kill loop + unlock confirm | Arm |
| Kill list preview | Chips + link to Blocks | Read `enforcer_policy` | — | Display |

---

## 6. Productive

### 6.1 Scoring — `ProductivityPolicyPanel` `variant="scoring"`

| Control | Presentation | Focus persistence | Notes |
|---------|--------------|-------------------|-------|
| Productive threshold | Number 1–100 | **Often Study-only / not native-written** | Native rollup may read `classify_rules.json` `productive_threshold` |
| Productive / blocked categories | Checkbox grids | **Not fully wired in Focus offline path** | Prefer Study API historically |
| Category scores | Sliders / numbers | Study `:8000` `/api/behavior/category-scores` | Study-only |
| App overrides | Key + category | Study path | Study-only |
| Save | Button | Partial in Focus | Prefer understanding Study vs native gaps |

### 6.2 Extras (`productiveExtra` from `ProductivitySettingsTab`)

| Panel | Presentation | Path | Class |
|-------|--------------|------|-------|
| `DesktopManagedBanner` | Banner → Overview | Nav | Copy |
| `SessionOverridePanel` | Pick session → mark productive / clear | Study PATCH tracked sessions | Study-only |
| `ActivitiesPanel` | Day table, uncategorized filter | Study activities API | Study-only |
| `ClassificationReview` | Scan / approve / reject LLM suggestions | Study classification API | Study-only |

---

## 7. More (tools)

### 7.1 Look — `SidebarTitleStickerPanel`

| Control | Persist | Class |
|---------|---------|-------|
| Calligraphy / cursive / brush font | `localStorage` `sidebarStickerFont` | Cosmetic |
| Title size (50–220%) | `localStorage` `sidebarStickerScale` | Cosmetic |
| Core text color | `localStorage` `sidebarStickerCoreFill` | Cosmetic |
| Contour rings (1–6): add / remove, dilation, color | `localStorage` `sidebarStickerLayers.rings` | Cosmetic |
| Built-in theme presets | Applies layers + core (optional font/scale) | Cosmetic |
| Save / delete custom presets | `localStorage` `sidebarStickerCustomPresets` | Cosmetic |

### 7.2 Perf — `FocusPerfReviewPanel`

| Control | Persist | Class |
|---------|---------|-------|
| Copy review JSON / Clear / latency tables | `localStorage` perf ring (`focusPerfLog`) | Diagnostic |

### 7.3 Planning — `PlanningSettingsPanel`

| Control | Write path | Backend | Class |
|---------|------------|---------|-------|
| Auto-apply routines on login | `localStorage` planning prefs | Read by app auth/bootstrap | Local |
| Apply once (today) / Force apply | `routine.apply` | Enforcer planner routines → `plan_blocks.json` | Plan |
| Clear today (Daily routines) | `plan.clear_day` | Deletes today's `planner_blocks` → `plan_blocks.json` | Plan |
| Lock in range (Today / Week / Month) | FE loops `routine.apply` per day (max 31) | Same mirror; weekday masks per day | Plan |
| Clear range | FE loops `plan.clear_day` | Clears blocks for selected days | Plan |
| Knob rows (morning gate, etc.) | Read-only distraction gate / defaults | Often “—” in Focus | Display |

### 7.4 Demo — `DemoModePanel`

Shown **only outside** Focus desktop shell. Fake SoftLand clock via Study `:8000`. **Hidden in Focus.**

### 7.5 Watch ↔ PC — `WearablesSyncPanel`

Token, ping, test ingest, status → Study wearables APIs + local token storage. **Study-backed.**

### 7.6 Plan reminders — `PlannerRemindersPanel`

| Control | Persist | Class |
|---------|---------|-------|
| Enable/Disable notifications | `localStorage` + Notification permission | Local |
| Lead minutes | `localStorage` | Local |
| Next block hint | `plan_blocks` mirror | Local |

**Caveat:** scheduler runs while this panel is mounted (Settings → More open).

### 7.7 Export (`ProductivitySettingsTab` toolsContent)

| Control | Path | Class |
|---------|------|-------|
| 7d / 30d / 90d / Year chips | Parent state on `ProductivityPage` | UI |
| From / To dates + `ExportRangeCalendar` | Parent state | UI |
| JSON / CSV (long) / CSV (wide) | Study `downloadProductivityWeekExport` `:8000` — v1.4 tidy `tables` + session rows; long CSV is multi-table (`# table:…`) | Study-only |

### 7.8 Setup card

Static copy: SelfTracker + Gate + `run_calt_desktop.bat`; Arm under Unlock.

---

## 8. EnforcerWriteGate

**FE:** `EnforcerWriteGate.tsx`, `useEnforcerReachability()`.

| State | UI |
|-------|-----|
| `unknown` | “Checking…” still interactive |
| `ok` | Normal |
| `down` / `no_bridge` | Banner + `pointer-events-none` opacity on children |

Design host `:5180` without WebView → gated sections behave read-only.

---

## 9. Gateway ops cheat sheet (Settings)

| Op | Typical UI |
|----|------------|
| `softland.set_enabled` | SoftLand on/off |
| `softland.patch_site_rules` | Allow/Watch/Block lists |
| `softland.patch_schedules` | Gate schedules |
| `softland.patch_mode_flags` | SoftLand mode flags |
| `softland.patch_goals` | Daily goal |
| `softland.spend_free` | Spend earned free |
| `softland.set_reward_day` / `reward.claim` | Reward day |
| `softland.set_day_pass` / `day.grant_pass` | Day pass |
| `arm.set` | Arm, lock, kill list |
| `device_block.*` | Device hosts lock |
| `routine.apply` | Apply routines |
| `plan.clear_day` | Delete all plan blocks for a local day |
| Routine Lock in / Clear range | FE calls `routine.apply` / `plan.clear_day` once per selected day (≤31) |

---

## 10. Known gaps / gotchas (code-truth)

1. **SoftLand ON ≠ Arm** — always show both; never imply one toggle does both.
2. **Dead SoftLand flags** — social/keywords/other/strict hidden (copy explains they did nothing).
3. **Productive scoring** — much of the rich category UI is Study `:8000` era; Focus offline path is thinner.
4. **Kill-list edits** — authoritative UI is Blocks → Apps (`arm.set`), not Unlock’s leftover chips.
5. **Secrets in `enforcer_policy.json`** — unlock password/phrase stored in plaintext today.
6. **Export / wearables / demo / classification** — need Study API; not enforcer SoT.
7. **Mode label vs decide** — Overview chip can disagree with full SoftLand decide ladder (schedules / morning).
8. **Device Status in Focus** — may reflect settings mirror more than live hosts parse.

---

## 11. Key source index

| Concern | File |
|---------|------|
| Settings IA / nav | `settings/make/MakeSettingsApp.tsx` |
| Tab extras (export, planning, …) | `ProductivitySettingsTab.tsx` |
| SoftLand policy UI | `ProductivityPolicyPanel.tsx` |
| Site rules | `SoftLandSiteRulesPanel.tsx` |
| Mode flags | `SoftLandModeFlagsPanel.tsx` |
| Schedules | `GateSchedulesPanel.tsx` |
| Kill list | `AppKillRulesPanel.tsx` |
| Device hosts | `DeviceBlockPanel.tsx` |
| Now / Arm | `FocusControlPanel.tsx` |
| Pipe client | `lib/enforcerNativeCmd.ts` |
| Gateway | `backend/calt_enforcer/src/cmd_gateway.cpp` |
| SoftLand decide | `backend/calt_msg_host/src/softland_decide.cpp` |
| Device block | `backend/calt_enforcer/src/device_block.cpp` |
| Policy / Arm JSON | `backend/calt_enforcer/src/policy_db.cpp` |
| SoftLand store | `backend/calt_enforcer/src/productivity_store.cpp` |
| Mirrors | `backend/calt_enforcer/src/softland_publish.cpp` |

---

*Doc generated for agent + human reference. Prefer this over Cold Turkey docs; no CT code was used.*
