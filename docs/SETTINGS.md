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

### 4.2 Apps — time limits — `AppTimeLimitsPanel`

Daily budget per exe. While minutes remain, the app can run. When today's foreground time reaches the budget, the enforcer kills that exe until local midnight. **Independent of Arm.** A free day / reward day does not restore a spent budget. Browsers are rejected (use Sites). Idle gaps over 5 minutes are not counted.

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| On / Off | Pill; applies on Save | `app_limits.set` | `app_limits.json` `enabled` | Time limit |
| Quick chips | Steam 1h, Discord 45m, Roblox 45m, Spotify 1.5h | Local until Save | — | Time limit |
| Per-app minutes | Number 1–1440 | Local until Save | `limits[].daily_minutes` | Time limit |
| Used / left bar | Polled from mirror (~10s) | Read | Tick writes `usage[]` | Display |
| Add exe | Name + minutes | Local until Save | — | Time limit |
| Save | Button | `app_limits.set` | `app_limits.cpp` kill when `used >= limit` | Time limit |

Mirror: `behavior/app_limits.json`. Live open window: `behavior/fg_live.json` (tracker, so the budget does not wait for a session flush).

### 4.3 Apps (Arm) — `AppKillRulesPanel`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Gaming / Social presets | Buttons merge exe lists | Local until Save | — | Arm |
| Exe chips | Click to remove | Local | — | Arm |
| Add exe | Input (+ `.exe`) | Local | — | Arm |
| Save | Button | `arm.set` / `putEnforcerPolicy` | `enforcer_policy.json` + runtime SQLite; kill loop in `win_service.cpp` | Arm |

SoftLand site domains never belong on this list.

### 4.4 Filters (Device) — `DeviceBlockPanel`

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
| Daily goal minutes | Number (≈15–960); Plan → Goals shows **goal / available** hours where available = free gaps between first→last routine on the day; stepper clamps goal ≤ that max when routines exist | `softland.patch_goals` (+ local goals key) | SoftLand `goals.daily_focus_minutes` | Goals |
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
| `ActivitiesPanel` | Day table, uncategorized filter | Focus: `day_activities.json` mirror; Study: activities API | Focus-native |
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
| **Require Confirm plan** | `softland.patch_goals` `planning_enabled` | SoftLand decide + DayLoopTick skip plan phase when false | Goals |
| Auto-apply routines on login | `localStorage` planning prefs | Read by app auth/bootstrap | Local |
| Apply once (today) / Force apply | `routine.apply` | Enforcer planner routines → `plan_blocks.json` | Plan |
| Clear today (Daily routines) | `plan.clear_day` | Deletes today's `planner_blocks` → `plan_blocks.json` | Plan |
| Planning range (Today / Week / Month) — Plan → Range | FE loops `routine.apply` per day (max 31) | Same mirror; weekday masks per day | Plan |
| Clear range days — Plan → Range | FE loops `plan.clear_day` | Clears blocks for selected days | Plan |
| Knob rows (morning gate, etc.) | Read-only distraction gate / defaults | Often “—” in Focus | Display |

When **Require Confirm plan** is off (default): Bible still runs; Confirm-plan overlay is hidden; after Bible, DayLoop auto-confirms plan and couples SoftLand+Arm. SoftLand decide treats plan as satisfied. Prefer Home **work sessions** as the daily focus loop; Plan/routines stay optional.

### 7.3b LLM host — `FocusLlmHostPanel`

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Use LLM for session Suggest + Jarvis | `localStorage` `calt:focus-llm-host:v1` | Hosted `/v1/chat/completions` (9Router / OpenRouter / custom) | Local |
| Preset 9Router / OpenRouter / Custom | same | Default base URLs | Local |
| Base URL, model, Bearer (OpenRouter `sk-or-…`) | same | Keys stay in this browser / on the host you run | Local |
| OpenRouter attribution | `HTTP-Referer` + `X-OpenRouter-Title` (+ legacy `X-Title`) | Optional rankings headers | Local |
| Test host | GET `{base}/models` | Ping only (OpenRouter needs Bearer) | Diagnostic |

**Indication:** `FocusAiStatusChip` in Settings LiveStatusStrip, Home mode header, Suggest rail, and LLM/Jarvis panels (OpenRouter / 9Router / off + optional ping).

Work session **Suggest** (Home allow/block rail) and **Jarvis** briefs call that host; on failure Suggest uses heuristics and Jarvis uses a canned line. See `services/llm_host/README.md`.

### 7.3c Jarvis — `FocusJarvisPanel`

Study voice_agent spirit (`jarvis` | `normal`). Default **Jarvis** picks Microsoft Ryan / en-GB (Study `en-GB-RyanNeural`); **Normal** picks Jenny / en-US. Speaks via WebView2 `speechSynthesis` (Edge neural voices when installed) — no Study `:8000` / edge-tts process.

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Enable Jarvis | `localStorage` `calt:focus-jarvis:v1` | Home card + brief | Local |
| Speak aloud | same | Web `speechSynthesis` | Local |
| Voice model jarvis \| normal | same (`voiceMode`) | Auto voice + rate/pitch presets | Local |
| Pinned voice | same (`voiceURI`) | Optional `speechSynthesis` voice | Local |
| Rate / pitch | same | Utterance tuning | Local |
| Test voice / Stop | — | Speak sample / cancel | Local |
| Auto morning brief | same | Once/day on Home | Local |
| Run brief | LLM host chat or canned | No Study `:8000` | Local |
| Command box | same UI | `focusCommands` → mirrors / enforcer / WhatsApp | Local |
| Mic (PTT) | same UI | Edge Web Speech (`focusStt`) | Local |

Study voice_agent used **faster-whisper** (Python) when installed, else SpeechRecognition. Focus uses **Web Speech** in WebView2 (no Python STT process).

Commands: `help`, `brief`, `status`, `report`, `speak on|off`, `voice jarvis|normal`, `session end`, `softland on`, `softland off UNLOCK`, `pass`. SoftLand off still requires typed `UNLOCK` (server-enforced).

Home also shows day-loop **Goals · todos** (`day.task_*`) plus Plan **side todos** (`productivity:goals:v1` `extraGoals`).

### 7.3d WhatsApp daily report — `FocusWhatsAppReportPanel`

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Enable daily send | `localStorage` `calt:focus-whatsapp-report:v1` | Auto-open while Focus running | Local |
| Members (name + E.164) | same (`members[]`) | One `wa.me` chat per member | Local |
| Send time (local) | same | Once/day window (~20m) | Local |
| Include top apps / todos | same | From `day_activities` / `day_loop` | Local |
| Preview / **Send report** | — | Compose + open all member chats | Local |
| Home Send report | compact card | Same send path (testing) | Local |

Opens WhatsApp with a prefilled report per member (tap Send). No Meta Business API / Study `:8000`.

### 7.4 Demo — `DemoModePanel`

Shown **only outside** Focus desktop shell. Fake SoftLand clock via Study `:8000`. **Hidden in Focus.**

### 7.5 Watch / Health — `WearablesSyncPanel` + sidebar **Health**

**Primary:** Amazfit **CALT Sync 4.3.2** Dump & Send (or Auto every 3h) → CALT Focus `:8765` → `calt_enforcer` → Life Tracker mirrors (`life_today.json`). The Python hub is only for NutriNode, and only when Focus is not holding `:8765`.

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Hub URL | `localStorage` `calt:wearables:hubUrl` | CALT Focus `:8765` → enforcer `wearable.ingest` | Local |
| Ingest token | `localStorage` `calt:wearables:token` | Bearer / `X-CALT-Wearable-Key` | Local |
| Auto sync | Watch `localStorage` + `@zos/alarm` | Dump & Send on wake | Watch |
| Life / Health UI | — | `GET /api/life/daily/*` + mirrors | Hub read |

See [`docs/WEARABLES.md`](WEARABLES.md).

### 7.5b NutriNode — sidebar **NutriNode** (`/nutrition`)

Meal search/log + daily macros via Focus hub `:8765` (`/api/nutrition/*`). Stores `behavior/nutrition_today.json`.

**Photo suggest:** camera/file → hub `POST /api/nutrition/analyze-photo` (Gemini vision) → suggested names + optional weight hint → user sets grams → Add → Send. Needs `GEMINI_API_KEY` or `LLM_CLOUD_API_KEY` in the hub process env, or `data/productivity/behavior/nutrition/nutrition_llm.json` (`{"gemini_api_key":"..."}`), or sibling Study `.env`. Pipeline CSV/ESP32 stay Study-only.

**Phone Chrome:** same hub on LAN → `http://<PC-LAN-IP>:8765/n` (Add to Home screen). Firewall once: `scripts/run/open_firewall_hub_8765.bat` as Admin. Windows webcam: `https://127.0.0.1:8766/n`.

Each meal stores local clock (`Asia/Kolkata`), weekday, capture path (webcam/gallery/manual), client, AI recognize ms, and suggested names. Append-only timeline: `behavior/nutrition/nutrition_events.jsonl` · `GET /api/nutrition/events`.

### 7.6 Plan reminders — `PlannerRemindersPanel`

| Control | Persist | Class |
|---------|---------|-------|
| Enable/Disable notifications | `localStorage` + Notification permission | Local |
| Lead minutes | `localStorage` | Local |
| Next block hint | `plan_blocks` mirror | Local |

**Caveat:** scheduler runs while this panel is mounted (Settings → More open).

### 7.3d Quit helper — `QuitHowToPanel`

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Why Quit is blocked | Reads `enforcer_status.json` + SoftLand | Native tray refuse while SoftLand or Arm on | Display |

Focus desktop only. No bypass — points to SoftLand UNLOCK + Disarm, then tray Quit.

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
| `softland.patch_goals` | Daily goal + `planning_enabled` (plan phase on/off) |
| `softland.spend_free` | Spend earned free |
| `softland.set_reward_day` / `reward.claim` | Reward day |
| `softland.set_day_pass` / `day.grant_pass` | Day pass |
| `arm.set` | Arm, lock, kill list |
| `app_limits.set` | Daily app time limits (`enabled`, `limits[]`) |
| `session.set` | Dashboard work session. Start/end open every day (session-first). |
| `softland.spend_free` | Retired (`spend_retired`). Balance does not open games or apps. |
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
6. **Export / demo / classification** — still Study API where noted. **Wearables** → Focus `:8765` hub (not Study `:8000`).
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
| App time limits | `AppTimeLimitsPanel.tsx` · `backend/calt_enforcer/src/app_limits.cpp` |
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
