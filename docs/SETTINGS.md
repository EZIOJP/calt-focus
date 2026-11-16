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

- Full-height panel (`min-h-[70vh]`) inside Productivity (`settings-chrome` tokens in `src/styles/settingsChrome.css`).
- **Desktop:** left rail nav (icon + label + blurb) — Overview · Blocks · Unlock · Day · Profile · Data · Advanced. **Mobile:** horizontal scroll rail.
- **Brand line:** title “Settings” + SoftLand ≠ Arm reminder.
- In Focus desktop shell: **LiveStatusStrip** above the section body (Site block · mode · Arm · enforcer · today minutes).
- Write sections (Blocks / Unlock) wrap body in **`EnforcerWriteGate`** — if enforcer/bridge is down, UI goes dim + non-interactive with a banner.
- **UX bar (ui-ux-pro-max):** ≥44px nav targets, focus-visible rings, job-based Overview jumps, no purple/AI chrome.

### 1.2 URL / deep links

| Query / hash | Resolves to |
|--------------|-------------|
| `section=overview` | Overview |
| `section=blocks` | Blocks → Sites |
| `section=sites` / `apps` / `filters` | Blocks sub-tab |
| `section=unlock` (also legacy `now`, `focus`, `enforcer`) | Unlock |
| `section=day` (also legacy `more` / `tools` / `watch` / `planning` / `body`) | Day |
| `section=appearance` (also `theme` / `ui` / `look` / `sticker`) | Appearance (theme + sidebar sticker) |
| `section=profile` (also `account` / `owner` / `me` / `user`) | Profile (local owner name/email/notes) |
| `section=data` (also `export` / `import` / `update` / `setup`) | Data |
| `section=advanced` (also legacy `productive` / `scoring` / `classification` / `demo` / `jarvis`) | Advanced |

Parser: `parseSettingsSection()` in `MakeSettingsApp.tsx`.

### 1.3 Information architecture

| Nav | Meaning |
|-----|---------|
| **Overview** | Status, SoftLand≠Arm callout, day flow, jump cards |
| **Blocks** | Edit what gets blocked (Sites / Apps / Filters) |
| **Unlock** | Free SoftLand types (goal / reward / pass) — not Arm |
| **Blocks → Apps** | Arm on/off + lock, kill list, time limits |
| **Day** | Body care, wearables, plan reminders, planning |
| **Appearance** | Light/dark, color presets, surfaces, typography, sidebar title sticker |
| **Profile** | Owner display name, email, timezone, notes (localStorage on this PC) |
| **Data** | Export, SoftLand import, UI update, setup blurb |
| **Advanced** | Perf, LLM+Jarvis, Telegram/email/OpenJarvis (collapsed), activity review |

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
| Day flow | Bible → Plan → Apply/Confirm; locks after free day/pass | static | — | Copy |
| Jump buttons | Grid to Blocks / Unlock / Day / Data / Advanced | `onJump` | — | Nav |

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
| Block social / keywords / other / Strict allowlist / sensitive | **Hidden** (not in UI) | May still exist in JSON | **Not enforced** as separate toggles | Dead UX |
| Save mode flags | Button | `softland.patch_mode_flags` | Gateway | SoftLand |

#### Schedule — `GateSchedulesPanel`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Enable recurring schedules | Checkbox | `softland.patch_schedules` | SoftLand decide `ResolveScheduleMode` | SoftLand |
| Windows | Label, start/end, mode (Study/Free/Planning/Bible), Mon–Sun, delete | `schedules.windows` | First matching window wins | SoftLand |
| Add window / Save | Buttons | same | Gateway | SoftLand |

### 4.2 Apps — Arm — `ArmControlCard`

Slim Arm on/off + lock mode (timer / password / phrase) + anti-tamper. Kill list CRUD stays in §4.3. SoftLand free does not disarm Arm. Needs free day / pass after Confirm to change Arm.

### 4.3 Apps — time limits — `AppTimeLimitsPanel`

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

### 4.4 Apps (kill list) — `AppKillRulesPanel`

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Gaming / Social presets | Buttons merge exe lists | Local until Save | — | Arm |
| Exe chips | Click to remove | Local | — | Arm |
| Add exe | Input (+ `.exe`) | Local | — | Arm |
| Save | Button | `arm.set` / `putEnforcerPolicy` | `enforcer_policy.json` + runtime SQLite; kill loop in `win_service.cpp` | Arm |

SoftLand site domains never belong on this list.

### 4.5 Filters (Device) — `DeviceBlockPanel`

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

## 5. Unlock — `UnlockGuidePanel`

Unlock explains **SoftLand free time** (until midnight). SoftLand free ≠ Arm. App time budgets still apply.

| Control | Presentation | Write path | Backend | Class |
|---------|--------------|------------|---------|-------|
| Goal met card | Progress vs daily goal (read-only); auto free when met | Read gate | SoftLand / day rollup | Display |
| Reward day card | **Count banked** + earn condition (goal+Bible × N days) + Use | `reward.claim` | Bank + free until EOD | Unlock |
| Day pass card | **Count left / week** + Bible condition + Use | `day.grant_pass` | Quota; free until EOD | Unlock |

Daily goal **minutes are not edited here** — change on Plan → Goals. Arm → **Blocks → Apps**.

---

## 6. Advanced — activity (was Productive)

Scoring grids removed from Settings (never drove SoftLand/Arm). Under **Advanced → Activity review**:

| Panel | Focus desktop | Class |
|-------|---------------|-------|
| `ActivitiesPanel` | `day_activities.json` mirror | Focus-native |
| `ClassificationReview` | OpenJarvis / enforcer classify path when available | Focus-native when LLM on |
| `SessionOverridePanel` | **Hidden** (Study `:8000` PATCH) | Study-only |

---

## 7. Look / tools (Advanced + Day + Data)

### 7.1 Appearance — `ThemeSettingsPanel` + `SidebarTitleStickerPanel`

Settings → **Appearance** (not Advanced). Light/dark works in Focus; shell CSS scopes pure-black stage to `.dark`.

| Control | Persist | Class |
|---------|---------|-------|
| Light / dark toggle | `localStorage` `theme` | Cosmetic |
| Color presets / accent / surfaces / type / motion / radius / intensity | ThemeContext keys | Cosmetic |
| Calligraphy / cursive / brush font | `localStorage` `sidebarStickerFont` | Cosmetic |
| Title size (50–220%) | `localStorage` `sidebarStickerScale` | Cosmetic |
| Core text color | `localStorage` `sidebarStickerCoreFill` | Cosmetic |
| Contour rings (1–6): add / remove, dilation, color | `localStorage` `sidebarStickerLayers.rings` | Cosmetic |
| Built-in theme presets | Applies layers + core (optional font/scale) | Cosmetic |
| Save / delete custom presets | `localStorage` `sidebarStickerCustomPresets` | Cosmetic |

Long-press the sidebar theme toggle → Appearance.

### 7.2 Perf — `FocusPerfReviewPanel`

| Control | Persist | Class |
|---------|---------|-------|
| Copy review JSON / Clear / latency tables | `localStorage` perf ring (`focusPerfLog`) | Diagnostic |

### 7.3 Planning — `PlanningSettingsPanel`

| Control | Write path | Backend | Class |
|---------|------------|---------|-------|
| **Require Confirm plan** | Forced on (`planning_enabled` true) | SoftLand decide + DayLoopTick. Bible morning, afternoon 12:00, evening 17:00, then Confirm | Goals |
| Auto-apply routines on login | `localStorage` planning prefs | Read by app auth/bootstrap | Local |
| Apply once (today) / Force apply | `routine.apply` | Enforcer planner routines → `plan_blocks.json` | Plan |
| Clear today (Daily routines) | `plan.clear_day` | Deletes today's `planner_blocks` → `plan_blocks.json` | Plan |
| Planning range (Today / Week / Month) — Plan → Range | FE loops `routine.apply` per day (max 31) | Same mirror; weekday masks per day | Plan |
| Clear range days — Plan → Range | FE loops `plan.clear_day` | Clears blocks for selected days | Plan |
| Knob rows (morning gate, etc.) | Read-only distraction gate / defaults | Often “—” in Focus | Display |

**Require Confirm plan** is forced on. Day path: **Bible → Plan → Apply/Confirm**. Until Confirm, Focus stays on Plan; **Planning prefs** opens as an in-gate sheet (gear on DayGateStrip / Plan) — not full Settings. Apply, Clear, and New routines work in that window. After Confirm, plan/lock edits need a free day or day pass. Sleep save stays open. Home does **not** create short/dynamic work sessions (End leftover only).

### 7.3b Focus AI brain — `FocusLlmHostPanel`

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Use LLM for session Suggest + Jarvis | `localStorage` `calt:focus-llm-host:v1` | OpenAI-compatible `/v1/chat/completions` | Local |
| Preset OpenJarvis / Ollama / OpenRouter / Custom | same | **Default: OpenJarvis** `http://127.0.0.1:8000/v1` + `qwen3.5:4b` | Local |
| Base URL, model, Bearer (OpenRouter `sk-or-…`) | same | Keys stay in this browser / host | Local |
| Test host | GET `{base}/models` (+ OpenJarvis `/health`) | Diagnostic | Local |

**Indication:** `FocusAiStatusChip` (OpenJarvis / Ollama / OpenRouter / off). Suggest + Jarvis call this host; SoftLand/Arm/Gate never do. Stock Ollama-only prefs migrate once to OpenJarvis; a one-shot re-enable turns a stuck AI-off back on. Jarvis chat auto-enables the brain if toggled off. In `calt_focus`, LLM HTTP is **native WinHTTP** (`llm_http` bridge) so WebView CORS cannot block `:8000`. See `services/llm_host/README.md`.

### 7.3c2 OpenJarvis — `FocusOpenJarvisPanel`

Settings → More: **Use as Focus AI brain** (points prefs at `:8000/v1`), model chips from `/v1/models`, desktop launch via `open_openjarvis`. **Auto-start API** runs background `jarvis start` (no GUI) via native `openjarvis_host` bridge when `:8000` is down. Auto-load / idle-unload use Ollama `:11434` keep_alive. Jarvis commands: `serve`, `model`, `load`, `unload`, `classify`. Classification approve → enforcer `classify.upsert_app_rule`.

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
| Command box | same UI | `focusCommands` → mirrors / enforcer / email report / `pc_power` | Local |
| Mic (PTT) | same UI | Edge Web Speech (`focusStt`) | Local |

Study voice_agent used **faster-whisper** (Python) when installed, else SpeechRecognition. Focus uses **Web Speech** in WebView2 (no Python STT process).

Commands: `help`, `brief`, `status`, `report`, `speak on|off`, `voice jarvis|normal`, `session end`, `softland on`, `softland off UNLOCK`, `pass`, `lock`, `sleep`, `shutdown CONFIRM`, `shutdown cancel`. SoftLand off still requires typed `UNLOCK` (server-enforced). Power ops run in `calt_focus` (`pc_power` bridge) — interactive session, not the enforcer service.

### 7.3c3 Telegram remote — `FocusTelegramRemotePanel`

Phone / internet control via Telegram Bot API long-poll inside **`calt_focus`** (no inbound port, not Study `:8000`, not enforcer).

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Enable bot | `%USERPROFILE%\.calt\telegram_remote.json` | Native poller | Local+Internet |
| Bot token | same (masked in status) | `@BotFather` token | Secret |
| Allow next chat | `link_next` then `/start` or `link` | Allowlist `allowed_chat_ids` | Local |
| Free chat | `allow_chat` | OpenJarvis `:8000` via WinHTTP | Local |
| Power commands | `allow_power` | `pc_power` lock/sleep/shutdown | Local |

Commands from Telegram (and desktop Jarvis): `help`, `status`, `summary`, `brief`, `plan`, `todos`, `export [days] [json|csv]`, `softland on|off UNLOCK`, `pass`, `winddown`, `session end`, `close browsers`, `close app name.exe`, `lock`, `sleep`, `shutdown CONFIRM`, `shutdown 5m CONFIRM`, `shutdown cancel`. **Arm hard-block is not exposed remotely.** Empty allowlist ignores strangers until **Allow next chat**. Rate-limit ~1 cmd/sec. Disabled by default. Close-apps never kills Focus/enforcer/WebView2/explorer.

Home also shows day-loop **Goals · todos** (`day.task_*`) plus Plan **side todos** (`productivity:goals:v1` `extraGoals`).

### 7.3d Email daily report — `FocusEmailReportPanel`

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Enable daily send | `localStorage` `calt:focus-email-report:v1` | Auto-open while Focus running | Local |
| To | fixed `JPMASTILA12@gmail.com` | `mailto:` compose | Local |
| Send time (local) | same | Once/day window (~20m) | Local |
| Include top apps / todos | same | From `day_activities` / `day_loop` | Local |
| Preview / **Send report** | — | Compose + open mail app | Local |
| Home Send report | compact card | Same send path (testing) | Local |

Opens the default mail client with a prefilled report to `JPMASTILA12@gmail.com` (tap Send). No WhatsApp / Meta Business API / Study `:8000`.

### 7.4 Demo — `DemoModePanel`

Shown **only outside** Focus desktop shell. Fake SoftLand clock via Study `:8000`. **Hidden in Focus.**

### 7.5 Watch / Health — `WearablesSyncPanel` + sidebar **Health**

**Only path:** Amazfit **CALT Sync 4.3** Dump & Send (or Auto every 3h) → Focus hub `:8765` → Life Tracker (`/life`). Google Fit, Health Connect, and Study `:8000` are not accepted.

Watch upload is `calt_sync_host` (`scripts\run\start_calt_sync_host.bat`), a separate process from the enforcer. It blocks on `:8765` and only works when a dump arrives. The watch still uploads on its own alarm (default every 3 hours).

| Control | Persist | Backend | Class |
|---------|---------|---------|-------|
| Hub URL | `localStorage` `calt:wearables:hubUrl` | `calt_sync_host` on `:8765` | Local |
| Ingest token | `localStorage` `calt:wearables:token` | Bearer / `X-CALT-Wearable-Key` | Local |
| Auto sync | Watch `localStorage` + `@zos/alarm` | Dump & Send on wake | Watch |
| Life / Health UI | — | Focus mirrors `life_today.json` / `life_days/` / `life_history_14.json` (enforcer); hub ingest only | Mirror read |

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

**Runtime:** `PlannerRemindersRuntime` in Focus shell (always-on while Focus is open). Panel under Settings → Day only edits prefs.

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
| JSON / CSV (long) / CSV (wide) | Focus: enforcer `productivity.export` → `behavior/productivity_export.{json,csv}`; desktop saves via native `behavior_file` copy to Downloads (calt-data fetch is fallback). Study: `:8000` `/api/planner/export/last-7-days` | Focus-native |
| Import SoftLand lists | File picker → `softland.patch_site_rules` from JSON `site_rules` / policy backup | Focus-native |
| Reveal last export | Native `behavior_file` reveal on `productivity_export.json` | Focus-native |
| Update check / Update UI / stack / Apply | `focus_shell` → tray UpdateUi / UpdateStack / ApplyPendingUpdate; reads `pending_update.json` | Focus-native |
| Body care — sit/stand | Settings → **Day**; `localStorage` `calt:bodycare:prefs` + Notification; optional presence-pause when cam live | Focus-native |
| Body care — eye rest | Same prefs; 20–20–20 nudges | Focus-native |
| Body care — attention reader | Opt-in webcam via face-tracker (lazy, shell-hosted); no SoftLand coupling | Focus-native |
| Body care — posture coach | Same cam; head-pitch score + hold warn (`postureWarnMinutes`, `postureScoreThreshold`) | Focus-native |

**Placement:** Day = body/watch/planning; Data = export/import/update/setup; Advanced = LLM/Jarvis/remote/activity (collapsed where noisy).

### 7.8 Setup card

Static copy under **Data**: SelfTracker + Gate + `run_calt_desktop.bat`; Arm under Unlock.

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
| `session.set` | Legacy work session op (FE create retired; End leftover on Home still calls end). |
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
4. **Kill-list / Arm** — authoritative UI is Blocks → Apps (`ArmControlCard` + `arm.set`), not Unlock.
5. **Secrets in `enforcer_policy.json`** — unlock password/phrase stored in plaintext today.
6. **Demo / classification** — still Study API where noted. **Export** is Focus-native (`productivity.export`). **Wearables** → Focus `:8765` hub (not Study `:8000`).
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
