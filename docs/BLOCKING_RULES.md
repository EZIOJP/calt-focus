# How blocking actually works

Written from the code, not from intent. If this file and the code disagree, the
code wins and this file is a bug. Last checked against source **2026-10-03**.

Read [The 60-second version](#the-60-second-version) if you just need to explain
it to someone. Everything after that is the exact rule, with the file that
decides it.

---

## The 60-second version

You have **two separate weapons**. They share intent in the UI; they do **not**
share a decision function:

| | Site block (SoftLand engine) | Arm |
|---|---|---|
| UI name | **Blocker** → sites (Allow / Block) | **Blocker** → apps (kill list) |
| Blocks | **websites**, in the browser | **apps**, at the OS level |
| Decided by | `calt_msg_host.exe` (`softland_decide.cpp`) via Gate | `calt_enforcer.exe` kill loop |
| Switch | `softland_enabled` | `hard_block_armed` |
| Reads | `data/productivity/behavior/softland_policy.json` | `data/productivity/behavior/enforcer_policy.json` |
| Can it kill Steam? | Never | Yes |
| Can it block YouTube? | Yes | Never |
| Knows mode / free / incubation? | Yes — stateful | No — binary armed × kill-list |

**Site block ON is not Armed.** Turning site blocking on does not arm anything,
and SoftLand / msg-host code is structurally forbidden from setting
`hard_block_armed`. That split is a **safety** decision (annoying vs
destructive), not a leftover from the old Python/extension split.

Site block is **stateful**: the same URL is allowed or blocked depending on
*mode*. Mode is not “study vs unlocked” — it is a **three-step override**:

1. Schedule (default `study` if no window)
2. Reward day or free window → `free`
3. **Incubation** → back to `study` (wins over free)

Site block is a **blocklist, not an allowlist**. An unlisted site is allowed.
Blocking comes from: SoftLand porn **heuristic**, your `block_extra`, and (in
study, when watch is on) `watch_extra` + the built-in distractor list.

**Porn is two layers** (do not conflate them in UI copy):

| Layer | Where | Respects SoftLand Allow? | Respects free / goal unlock? |
|-------|--------|---------------------------|------------------------------|
| SoftLand porn **heuristic** | Host ladder rule 2 in `softland_decide.cpp` | Yes — `allow_extra` beats it | No — stays on in free (with `block_porn`) |
| Windows **hosts-file** device block | `calt_enforcer` `device_block.*` → `hosts` | **No** | **No** — always on for those domains |

**The day gets unlocked three ways, and only three:** hit your daily minutes
*and* tick a Bible chapter; spend a reward day (4 qualifying days buys one); or
spend a day pass (2 a week). Separately, small chores pay out **earned minutes**
(15 for Bible, 10 for the plan, 30 for the goal, 60/day cap) that you spend in
15-minute chunks. Passes, reward claim, earn events and incubation limits are
enforced by `calt_enforcer` (work with `:8000` stopped). **P5c (2026-09-11):**
Study `distraction_gate` / `goals_alerts` **read** `data/productivity/behavior/day_rollup.json`
when present and fresh (legacy recompute only as fallback). Native qualification
/ streak credit without `:8000` remains a later P5c slice.

---

## Naming (UI vs code)

| User-facing | Code / SoT / gateway (keep forever) |
|-------------|-------------------------------------|
| **Site block** / **Blocker** (sites) | SoftLand: `softland_enabled`, `softland_policy.json`, `softland_decide.cpp`, `softland.*` ops |
| **Arm** / **Blocker** (apps) | `hard_block_armed`, `enforcer_policy.json` `exes`, `kill.cpp` |
| Device porn (hosts) | Settings only — not Plan Goals Blocker copy |

“SoftLand” was the name for browser-site blocking when it was split across
extension + Python. The decide path is now Gate → `calt_msg_host` → C++. Prefer
plain terms in UI and docs for humans; keep `softland_*` identifiers in code so
migrations and mirrors stay stable.

**Do not merge** site decide and Arm kill into one verdict function. Wrong site
verdict self-corrects on the next navigation; wrong kill is destructive. If you
share code later, share only a **mode resolver** (schedule / free / incubation →
current mode) — not the host ladder and the kill-list check.

---

## Goals “Blocker” board (product shape)

Plan → Goals may show one **Blocker** control surface. That is **UX grouping**,
not one engine:

| Column / action | Sites | Apps |
|-----------------|-------|------|
| **Allow** | → `site_rules.allow_extra` | Remove from kill list (`exes`) |
| **Block** | → `site_rules.block_extra` | Add to kill list (`exes`) |

- **Study-only** (`watch_extra` + built-in distractors) stays out of this board
  (Settings / engine defaults). They open in SoftLand `free` mode unless
  incubation is on.
- **Block** sites stay blocked after unlock (rule 3 beats free). **Block** apps
  stay killable while Armed — focus/goal met does **not** disarm Arm.
- **Allow** always wins on the SoftLand host ladder (including SoftLand porn
  heuristic). It does **not** punch through the hosts-file device block.
- Safe short copy for that board: *“Block stays after focus unlock; Allow always
  wins in Site block. Apps = Arm kill list. Device porn (hosts) is separate.”*
  Do not say “Allow always wins over porn” without meaning the SoftLand
  heuristic only.

---

## SoftLand (Site block): the exact decision ladder

One file decides: `calt-focus/backend/calt_msg_host/src/softland_decide.cpp`
(`SoftlandGetMode`). The Gate extension asks it per navigation and blocks only
when the answer is `action: "block"` **and** `enforce` is not `false`.

### Step 0 — can the policy be read at all?

| Situation | Result | `reason` |
|-----------|--------|----------|
| `softland_policy.json` missing | **Block everything** | `softland_policy_missing` |
| File there but `softland_enabled` unreadable | **Block everything** | `softland_policy_corrupt` |
| `softland_enabled: false` | Allow everything, `enforce: false` | `softland_off` |

Missing or corrupt policy **fails closed** — a broken file must not open the
whole web. This is deliberate.

### Step 1 — what mode is it right now?

In this order, each line able to overwrite the one above:

1. **Schedule window.** If `schedules.enabled`, the **first** window whose
   `days` include today (Mon=0 … Sun=6) and whose `start`–`end` contains now
   supplies the mode. Windows may cross midnight (`22:00`–`06:00` works).
   No match, or schedules off → `study`.
2. **Reward day or free window** → `free`.
   A free window means `runtime.free_until` is in the future, **or** the clock
   has passed `runtime.free_after_hm`.
3. **Incubation** (`runtime.incubation_until` in the future) → back to `study`.
   Incubation always wins; it is the "you just tried to relapse, sit still"
   state.

The banner text picks its reason in the same priority: incubation, then reward
day, then free window — so during a cooldown you always see *why* you are stuck,
not the nicer reason underneath it.

**UI copy:** do not reduce this to only “study vs after unlock.” A user in
incubation during an earned free window will see distractors closed again; the
reason is incubation, not “study schedule.”

### Locked-page / voice reason vocabulary (Phase 3)

Native SoftLand decide (`softland_decide.cpp`) and the Gate extension
(`blockKindForUrl`) historically used overlapping but not identical tokens.
Shared tokens today: `porn`, `watch_list`, `incubation`.

| Native `reason` | Extension `blockKind` / voice alias |
|-----------------|-------------------------------------|
| `porn` | `porn` |
| `watch_list` | `watch_list` / `watch` |
| `incubation` | `incubation` |
| `block_extra` | maps to `watch_site_block` in voice |
| `not_listed` / study catch-all | extension uses `study_block` / `softland_block` |
| `free_window` / `reward_day` / `allow_list` | allow paths (no lock page) |
| `morning_bible` | Gate interstitial / Focus morning overlay. `morning_plan` is no longer emitted. |

**Do not drive user-facing Blocker copy from live `reason` strings** until native
and any HTTP-fallback vocab are fully aligned; prefer the static wording in
[Goals “Blocker” board](#goals-blocker-board-product-shape).

Voice canonicalization: `backend/behavior/voice_agent/block_dialogues.py` → `KIND_ALIASES`.

### Step 2 — the host ladder, first match wins

Host is lowercased with scheme, port and a leading `www.` stripped. Matching is
exact host **or** any subdomain (`docs.google.com` matches `*.docs.google.com`).

| # | Rule | Verdict | `reason` |
|---|------|---------|----------|
| 1 | `site_rules.allow_extra`, or `localhost` / `127.0.0.1` | **Allow** | `allow_list` |
| 2 | SoftLand on and Bible **not done for local today** (and not emergency). A skipped plan does not block. | **Block** | `morning_bible` |
| 3 | Looks like porn, and the mode's `block_porn` is on | **Block** | `porn` |
| 4 | `site_rules.block_extra` | **Block** | `block_extra` |
| 5 | Mode is `free` and not incubating | **Allow** | `reward_day` / `free_window` / `free_mode` |
| 6 | Mode's `block_watch_sites` is on, and host is in `watch_extra` **or** the built-in list | **Block** | `watch_list` |
| 7 | Anything else | **Allow** | `not_listed` / `default_allow` |

### Reason vocabulary (native SoftLand vs Gate `blockKind`)

Native `get_mode` replies with short SoftLand `reason` tokens from
`softland_decide.cpp`. Gate’s `blockKindForUrl` (locked interstitial / voice)
uses a related but not identical set. Reconcile when comparing logs:

| Native SoftLand `reason` | Typical Gate `blockKind` / voice alias |
|--------------------------|----------------------------------------|
| `allow_list` | (allow — not blocked) |
| `porn` | `porn` / `keyword` |
| `block_extra` | `softland_block` / `generic_rule_break` |
| `reward_day` / `free_window` / `free_mode` | (allow in free) |
| `watch_list` | `watch` / `watch_list` |
| `not_listed` / `default_allow` | (allow) |
| `incubation` | `incubation` |
| `softland_off` | SoftLand disabled |
| `softland_policy_missing` / `_corrupt` | fail-closed |

Voice `KIND_ALIASES` in `backend/behavior/voice_agent/block_dialogues.py` maps
these native tokens into dialogue pools so Jarvis does not fall through on
unknown reasons.

Consequences worth saying out loud, because they surprise people:

- **SoftLand porn heuristic and `block_extra` beat free mode.** Rules 2 and 3
  come before rule 4, so a reward day does not open them. That is the intended
  asymmetry. (Hosts-file porn is separate and never opens.)
- **`watch_extra` is study-only.** A reward day, a free window or a `free`
  schedule window lets everything on the watch list through (rule 4 fires
  first). If you want a site blocked *even on a reward day*, it belongs in
  `block_extra`, not `watch_extra`.
- **`allow_extra` beats the SoftLand porn heuristic** (not the hosts file).
  Rule 1 is above rule 2. See [Sharp edges](#sharp-edges).
- **While incubating, every block reads `incubation`**, even if what technically
  matched was the watch list — the urgent reason is not overwritten by a
  lesser one. Incubation re-closes watch/built-in distractors even if free was
  already earned.
- **"Goal met → only porn + hard blocks left"** is true only if “porn” means
  the SoftLand heuristic (plus `block_extra`). Hosts-file domains stay dead.
  Arm kill list is untouched by SoftLand goal state.

### The built-in watch list

Hardcoded in `softland_decide.cpp`, always in effect when
`block_watch_sites` is on, and **not editable from Settings**:

`youtube.com`, `youtu.be`, `netflix.com`, `primevideo.com`, `hotstar.com`,
`disneyplus.com`, `hulu.com`, `twitch.tv`, `reddit.com`, `twitter.com`, `x.com`,
`instagram.com`, `facebook.com`, `tiktok.com`, `discord.com`

The only way to unblock one of these in study mode is to put it in
`allow_extra`.

### The SoftLand porn heuristic (not hosts)

Heuristic, not a list: host ends in `.xxx` / `.porn` / `.sex`, or contains
`porn`, `xvideos` or `pornhub`. It is on by default in **both** study and free
mode. It is **not** the same thing as the hosts-file porn block (see
[Hosts porn block](#hosts-porn-block)). SoftLand Allow can override this
heuristic; SoftLand Allow **cannot** override hosts.

### Mode flags

`mode_flags.<mode>` overrides the defaults per mode. Unknown modes
(`planning`, `bible`) fall back to the `study` block.

| Flag | Study default | Free default | Actually used? |
|------|---------------|--------------|----------------|
| `block_porn` | on | on | **Yes** — rule 2 |
| `block_watch_sites` | on | off | **Yes** — rule 5 |
| `block_social` | on | off | No — social sites are covered by the built-in watch list |
| `block_keywords` | on | on | No — no keyword rule exists in the decide path |
| `block_other` | on | off | No real effect |
| `strict_allowlist` | on | off | No real effect — see below |

`strict_allowlist` does **not** create allowlist-only browsing. Unknown sites
are allowed regardless (rule 6). If you want allowlist-only study mode, that is
a feature to build, not a checkbox to flip.

---

## Arm: the exact kill rules

`calt_enforcer.exe` owns this end to end and needs **no Python running**.
Arm does **not** use SoftLand mode, free windows, or incubation. Armed or not;
kill-list match or not. Do not give apps the site mode ladder unless that is an
explicit product change later.

- Reads `data/productivity/behavior/enforcer_policy.json` every tick (~1.5s). The JSON is
  authoritative; the SQLite row is a fallback when the file is missing.
- While `hard_block_armed` is true, it enumerates processes and terminates any
  whose executable base name matches your `exes` list, then appends to
  `data/productivity/behavior/enforcer_kills.log`.
- **Protected, never killed:** core Windows processes (`explorer.exe`,
  `csrss.exe`, `winlogon.exe`, `services.exe`, `lsass.exe`, `svchost.exe`,
  `smss.exe`, `fontdrvhost.exe`, …) plus `calt_focus.exe` and
  `calt_msg_host.exe`. Browsers are *not* blanket-protected — list a browser and
  it dies.
- **Lock modes** (`lock_mode`): `none`, `timer` (until `lock_until_unix`),
  `password`, `phrase`. A disarm that does not satisfy the lock is refused and
  the enforcer forces `armed + locked` back on, so editing the JSON by hand does
  not get you out. A **gateway** disarm that passes `provided_unlock` clears
  `lock_mode` to `none` (keeps the stored password/phrase for the next Arm) so
  sticky lock cannot immediately re-arm after a valid unlock.
- **Focus watchdog:** while SoftLand is on *or* Arm is on, `calt_focus.exe` and
  `calt_msg_host.exe` are relaunched if they die — at most 3 times in a rolling
  60s window, after which relaunching is suppressed (visible as
  `focus_relaunch_suppressed` in status) so a crash loop cannot spin forever.
- **Focus refuses to quit** from the tray while SoftLand is on or Arm is armed.
  Turn SoftLand off and disarm first.

---

## Hosts porn block (Device lock)

A third, independent thing, easy to confuse with the SoftLand porn heuristic.
**UI:** Focus → Settings → **Filters** → Device lock. Type `DEVICE LOCK` to enable or remove.
**SoT (native):** `data/productivity/behavior/device_block.json` + `porn_blocklist.json`, mutated only by
`calt_enforcer` (`device_block.*` gateway ops + elevated `--device-block-* --confirm`).
**Apply:** Admin via `scripts\device_block_apply.bat` / `remove` (pass confirm; bats re-pass after UAC).
Study `/api/behavior/device-block*` returns **410** (retired). Tracker hourly Python sync is a no-op.

So a porn domain can be blocked by up to three separate things at once: the
hosts file, the SoftLand porn heuristic, and the extension's own rules. Turning
one off does not turn the others off.

**Out of scope for Plan Goals Blocker copy** — keep hosts porn in Settings /
Filters only. Saying “Allow always wins” on the Goals board must not imply
hosts is overridden.

---

## Goals, earning and unlocks

Day passes, reward credits, earned-minute rates/caps, and incubation limits are
**enforced by `calt_enforcer`** (gateway ops on `\\.\pipe\calt_enforcer_cmd`) and
work with `:8000` stopped. SoftLand decide already honours `runtime.free_until`
and `reward_day_active`. **P5c reader path (2026-09-11):** productive minutes for
Study UI come from enforcer `day_rollup.json` when fresh; Python still combines
them with Bible chapter for unlock / streak recording until native qualification
owns that end-to-end.

### Focus day loop (2026-09-11; finalize 2026-10-03)

Morning (Focus shell, offline SoftLand + Arm):

```text
New calendar day (DayLoopTick)
  → clear sticky bible/plan/goal flags; clear stale free_until / goal_free_granted_date
  → DayLoopTick does not auto-confirm, does not turn SoftLand on, and does not Arm
  → SoftLand get_mode: non-allow hosts → morning_bible only (skipped plan is not a block)
  → Focus MorningBibleOverlay until bible_done_for_date == today, if SoftLand is already on
  → User confirms a plan → SoftLand ON + Arm ON
  → That confirm seeds the kill list (games/social presets + cursor.exe) if empty / missing cursor
  → If the user never confirms, the day stays open (lazy day): no default kill list, no morning_plan
  → After an explicit confirm: sites + cursor blocked until productive minutes ≥ daily_focus
  → DayLoopMaybeGrantGoalFree → free_until EOD; drop cursor.exe from kills (games/social stay)
```

**Cursor trade-off:** Cursor is on the kill list until goal met, so Cursor itself
does not earn unlock — other productive tracking does.

**Reward / day pass:** `reward.claim` and `day.grant_pass` require Bible done
today (`bible_required`); both clear study-temp kills (cursor) when granting free.

Landing: `DayLoopLanding` — tasks, SoftLand/Site-block mode, planned vs tracked.

**Dual-gate 1h free** (`day.evaluate_close`): checkboxes done (or zero tasks) **and** tracked ≥ 50% of today’s planned non-free block minutes → `free_until = now+60m` once per day. No SoftLand time-extend on incomplete tasks. **No plan carry-forward** — `plan.import_from_date` / `plan.roll_forward` return `carry_disabled`; shutdown may drop or leave-on-today only.

**Confirm plan** requires bible done today **and** ≥1 non-free planned minute today (`plan_required`). SoftLand stays **on** for the morning host gate; **Arm stays off** until Confirm (DayLoopTick disarms when `plan_confirmed_for_date ≠ today`). **Follow** (`ApplyActivePlanToSoftland`) runs only when `plan_confirmed_for_date == today`.

**Bedtime:** `softland.set_bedtime` → tick sets `bedtime_active` → Focus `BedtimeOverlay`. **Emergency:** `softland.emergency_winddown` (confirm `EMERGENCY`) opens web minus distraction lists; Arm keeps game kills.

**Focus FE:** Home stats / schedules / SoftLand lists read enforcer mirrors +
gateway only — no live Study `:8000`. Demo clock hidden in Focus Settings.

Spec: [2026-09-11-focus-day-loop-design.md](superpowers/specs/2026-09-11-focus-day-loop-design.md).

> **P5a done (2026-09-11).** See the
> [P5 design](superpowers/specs/2026-09-08-calt-productivity-p5-native-unlock-accounting-design.md)
> and the [P5a plan](superpowers/plans/2026-09-08-calt-productivity-p5a-native-unlock-accounting.md).
> Study UI still calls thin Python helpers that forward to the gateway.

### What unlocks the day

**Product rule** (same formula whether Study or enforcer evaluates it):

```text
day_unlimited = reward_day OR day_pass OR (productive >= goal AND chapter_met)
```

**Owner (2026-09-12):** SoftLand / Focus unlock qualification belongs in
**C++** (finish P5c). Study `distraction_gate.py` still implements this for
Study UI chips — that is **legacy**, not SoftLand truth. SoftLand free/reward
already honour enforcer `runtime` / gateway. Focus day loop also grants
`free_until` via `day.evaluate_close` (separate 1h free door).

Three independent doors, any one of which opens the day:

| Door | Requirement | Enforced today |
|------|-------------|----------------|
| **Earn it** | productive ≥ daily goal **and** ≥ 1 Bible chapter | Productive → `day_rollup`; chapter + full native qualify = P5c WIP |
| **Spend a reward day** | credit from 4-day streak | Enforcer gateway (**P5a**) |
| **Spend a day pass** | 2 per Mon–Sun week | Enforcer gateway (**P5a**) |

- **Daily goal** default **240 min** (`goals.daily_focus_minutes` / policy), editable.
- **Productive minutes** = scored sessions (enforcer rollup when fresh).
- **Chapter** = 1 chapter ticked in Focus/Bible path — dwell / PDF page turns do not count.

### Reward days — the 4-day streak

- A day **qualifies** when you hit both halves: productive ≥ goal *and* the
  chapter. Recorded once per day (target: enforcer **P5c**; until then Study may
  still call `reward.mark_qualified` via thin helpers). Enforcer stores the
  credit via `reward.mark_qualified`.
- **A day spent on a reward day never qualifies**, so you cannot farm streaks
  out of your days off.
- **4 qualifying days = 1 reward day**. `available = earned + granted − spent`
  lives in SQLite `productivity_reward_*` tables.
- **Claiming** takes the typed phrase **`REWARD`** (enforced natively on
  `reward.claim`), opens `free_until` until local midnight, and sets
  `reward_day_active`.
- **It ends at local midnight**, by SoftLand tick clearing expired
  `free_until` / `reward_day_active`. There is no "end reward day" button.

### Day pass — the deliberate skip

Two per Mon–Sun week, confirmed by typing **`PASS`**. `day.grant_pass` records
the pass in `productivity_day_passes`, writes `runtime.day_pass` as audit, and
**opens `free_until` to local 23:59:59** — the same free-window SoftLand decide
already honours. Unlike a reward day it does **not** excuse you from the morning
Bible and plan redirects. That asymmetry is intentional: a pass buys the day,
not the morning.

### Earned minutes — the small change

Credited natively by `day.mark_event` (rates copied from
`backend/behavior/break_reward.py`):

| Action | Earns | How often |
|--------|-------|-----------|
| Bible done (`chapter_done`) | **15 min** | once a day |
| Plan confirmed | **10 min** | once a day |
| Daily goal hit | **30 min** | once a day |

Capped at **60 min earned per day**. Spending (default 15 min at a time) debits
the ledger via `softland.spend_free` and opens/extends a free window; it is
**refused while incubating**. On the native side, spend always **extends** the
current free window — spending 15 minutes during a reward day can never shorten it.

### Incubation — the cooldown

- Starts either when a **study block ends** or after a **productive streak** of
  `work_minutes` (**45 min**).
- Lasts `break_minutes` (**8 min** default on `softland.set_incubation`).
- **At most 1 per rolling hour** — enforced natively (`incubation_rate_limited`).
- While it runs: mode is forced to `study`, and spending earned minutes is
  refused.
- Clear with `softland.clear_incubation` (gateway); there is still no casual UI
  cancel.
### Evening free and the study-loop gate

- **Evening free:** after `BROWSER_FREE_AFTER` (default **21:00**) the Python
  browser gate reports mode `free`. Native reads the same idea from
  `runtime.free_after_hm`.
- **Study-loop gate** is **off by default**. When on, and the plan is confirmed
  and the daily bite is unfinished, the morning step becomes `study` — only
  `/bible`, `/productivity`, `/profile` and `/review` stay reachable, redirecting
  to `/review?tab=loop`. It never writes the SoftLand policy itself.

---

## Where the state lives, and who may change it

```text
SoT (the truth)      → SQLite productivity_* tables in data/vocab_app.db
Only mutator         → calt_enforcer gateway, named pipe \\.\pipe\calt_enforcer_cmd
SoftLand hot read    → data/productivity/behavior/softland_policy.json   (mirror, published by enforcer)
Kill hot read        → data/productivity/behavior/enforcer_policy.json   (mirror, published by enforcer)
Health               → data/productivity/behavior/enforcer_status.json   (written by enforcer)
Kill history         → data/productivity/behavior/enforcer_kills.log
```

The UI never writes policy files directly. `calt_focus` posts a command into the
pipe, the enforcer applies it to SQLite and republishes the mirrors. Python may
still write `softland_policy.json`; the enforcer imports it whenever the file's
`updated_at` is newer than its own row, so nothing is lost in either direction.

The enforcer also runs a **clock tick**: expired incubation and free windows
clear themselves, and delayed edits (`productivity_pending_changes`) apply when
their `apply_after` passes — with no window open and no Python running.

---

## Settings, control by control

| Control | What it really does |
|---------|---------------------|
| SoftLand / Site block on/off | Master switch for **site** blocking only. Off = every site allowed (`softland_off`) |
| Allow extra | Rule 1 — wins over SoftLand host ladder, including SoftLand porn heuristic (not hosts) |
| Watch extra | Rule 5 — study mode only (opens in free unless incubating) |
| Block extra | Rule 3 — every SoftLand mode, including reward days / free |
| Gate schedules | Supplies the mode by day + time; first matching window wins |
| Mode flags | Only `block_porn` and `block_watch_sites` change behaviour today |
| Arm / Disarm + kill list | The OS killer and its target list — no SoftLand free/incubation |
| Lock mode / anti-tamper / protect uninstall | How hard it is to undo an Arm |
| Daily goal (min) and Plan's "daily focus h" | The **same** unlock target under two names — default 240 min |
| Category scores / productive threshold / app overrides | Feed the productive-time maths. **P5b** classifies in enforcer; **P5c** Study reads `day_rollup.json` (legacy recompute = fallback) |
| Reward days / day pass / earned minutes / incubation limits | Unlock currencies — enforcer-owned (P5a). Thin Python callers only |
| Study-loop gate | Off by default; when on it forces the morning into the daily bite |
| Device porn block (hosts) | Separate OS-level filter, all apps, needs admin, unrelated to SoftLand Allow/Block |
| Demo mode (fake clock) | Testing aid; moves the clock the rules read |

Pass/reward/earn/incubation keep working when `:8000` is down (enforcer gateway).
Productive minutes for Study gate/goals prefer `day_rollup.json` (P5c); Bible +
streak recording still touch Python until full native qualification.

---

## Sharp edges

Real behaviours that a reasonable person would guess wrong. None of these are
speculation; each was read out of the code.

1. **`allow_extra` disables the SoftLand porn heuristic for that host** — not
   the hosts-file device block. The allow list is checked first so it can rescue
   a false positive (the filter is a substring heuristic), but it also means
   the SoftLand allow list is a self-sabotage hatch for that heuristic. If you
   want SoftLand porn blocking to be unconditional, rule 2 has to move above
   rule 1 — an intentional decision, not a bug fix, so it is left to you.
2. **`watch_extra` does nothing on a reward day.** Use `block_extra` for
   "always blocked".
3. **`strict_allowlist`, `block_other`, `block_social`, `block_keywords` are
   inert.** They are stored and shown but no rule consults them (social is
   handled by the built-in list). Do not tune them expecting an effect.
4. **The built-in watch list cannot be edited**, only overridden per host via
   `allow_extra`.
5. **Two names, one number:** Plan's "daily focus h" and Settings' "Daily goal
   (min)" are the same unlock target.
6. **Arm has a fail-open window:** if the enforcer process is not running,
   nothing kills anything. That is why it holds an ownership lock, gets
   relaunched, and should be installed as a service for stay-alive.
7. **"Goal met" and "day unlocked" can still disagree on Bible.** Unlock
   (`distraction_gate`) still requires a Bible chapter on top of productive
   minutes. The `goal_met` chip (`goals_alerts`) only checks productive time
   (now from `day_rollup` when fresh) and **ignores the Bible**. So the chip can
   read "goal met" while the day is still locked.
8. **Claiming a reward day is refused on a day you already unlocked by working.**
   That is deliberate — it stops you burning a hard-won credit on a day you had
   already earned — but the message ("Today is already unlocked") reads like an
   error rather than a save.
9. **Incubation cannot be cancelled, by anyone.** No endpoint, no UI, and
   `allow_snooze` does nothing. Once it starts you wait the 8 minutes.
10. **The legacy Bible "game bank" is dead.** 30-minute chunks are still tracked
    and stored, but `has_bank` is hardcoded `False`, so banked time unlocks
    nothing. Chapter + study minutes is the only earn path.
11. **`softland_policy.goals.goal_met` is never written by Python.** It exists in
    the schema and normalises to `False` on read. Do not build UI on it.
12. **P5c reader path landed (2026-09-11).** `distraction_gate` and
    `goals_alerts` prefer `data/productivity/behavior/day_rollup.json` for productive minutes
    when the mirror is present, for today, and fresh (~120s). Missing/stale →
    legacy Python recompute. Bible chapter + streak *recording* still run in
    Study until native qualification finishes the P5c exit.

### Fixed on 2026-09-08 while writing this

Two rules did not match any reasonable description of them, and both are now
corrected in `softland_decide.cpp`:

- **Only the first schedule window was ever evaluated.** The window scan started
  on the `schedules` object itself, so window 1's fields were read and the loop
  then ran off the end. A "weekday focus + evening free" pair silently never
  entered evening free. Now the `windows` array is iterated properly, and `days`
  is parsed as numbers instead of searching the raw text for a digit.
- **`until` stamps were read as UTC.** Both free windows and incubation ran for
  the whole UTC offset longer than the UI promised — 5h30m here, so a reward day
  labelled "until 23:59" actually kept the web open until 05:29. Bare stamps are
  now read as local time and `+05:30` / `Z` suffixes are honoured. (The enforcer
  tick, which compares local time correctly, had been masking this whenever it
  was running.)

---

## Verify any of this yourself

No browser and no `:8000` needed:

```bat
:: what SoftLand thinks about one URL, through the real Gate path
powershell -File scripts\desktop_tracker\run\msg_host_cmd.ps1 -Json "{\"type\":\"get_mode\",\"url\":\"https://youtube.com\"}"

:: current SoftLand state straight from the SoT
powershell -File scripts\desktop_tracker\run\gateway_cmd.ps1 -Op status.snapshot

:: earned-minute ledger
powershell -File scripts\desktop_tracker\run\gateway_cmd.ps1 -Op ledger.snapshot -Payload "{\"limit\":10}"
```

To try rule changes without touching your live day, point the host at a fixture:

```powershell
$env:CALT_DATA_DIR = "$env:TEMP\calt_try\"   # needs behavior\softland_policy.json inside
powershell -File scripts\desktop_tracker\run\msg_host_cmd.ps1 -Json '{"type":"get_mode","url":"https://youtube.com"}'
Remove-Item Env:CALT_DATA_DIR
```

The reply's `reason` tells you which ladder rung fired, and `mode` tells you
which mode the clock and schedule put you in.

---

## Files that decide things

**Hot path (must work with Study `:8000` stopped):** C++ + Gate only.

| Question | Owner today | Target (locked) |
|----------|-------------|-----------------|
| Is this URL blocked? | `calt-focus/…/softland_decide.cpp` | Same — already C++ |
| Does the browser obey? | Gate extension | Same |
| Is this app killed? | `calt-focus/…/kill.cpp` | Same |
| Who may change SoftLand/Arm policy? | Enforcer gateway (`cmd_gateway.cpp`) | Same. Focus UI → pipe only |
| When do timers expire? | `softland_tick.cpp` | Same |
| Day pass / reward / earn / incubation | Enforcer gateway (**P5a**) | Same |
| Productive minutes rollup | Enforcer `day_rollup.json` (**P5b/P5c reader**) | Same — expand scoring in C++ |
| Hosts-file porn | `calt_enforcer` device_block (Admin CLI) | Native SoT — **not** SoftLand |

**Study Python leftovers (not the Gate brain — remove from SoftLand/unlock story):**

| Leftover | What it actually is | What to do |
|----------|---------------------|------------|
| `softland_policy.py` | Compat shim: load/patch/migrate mirrors for Study HTTP APIs. **Not** live `get_mode`. | Stop Study SoftLand mutators; Focus already uses gateway. Delete or shrink to migrate-only when Study interstitial is clean. |
| `distraction_gate.py` “day unlocked?” | Study UI still combines Bible + productive time for **Study** unlock chips / morning. SoftLand free/reward already honour enforcer runtime. | **Move qualification to C++ (finish P5c).** Study must not own “day unlocked.” Focus reads enforcer status / day loop. |
| `reward_days.py` / `break_reward.py` | Legacy accounting; rates copied into enforcer | Thin callers only → delete once nothing imports them for Focus |
| `bible.store` in unlock paths | Study still consulted in some gate paths | Bible ticks → Focus/enforcer (`goals.bible_done`); drop Study from SoftLand unlock |
| `productivity_policy.py` / `category_scores.py` | Study scoring / fallback recompute | Prefer enforcer rollup; legacy fallback only until P5b scoring complete |

**Product rules:** [AGENTS.md](../AGENTS.md), [Phase 2 gateway](superpowers/specs/2026-09-08-calt-productivity-phase2-gateway-design.md), [Focus standalone](superpowers/specs/2026-09-11-calt-focus-standalone-productivity-design.md), [P5 unlock](superpowers/specs/2026-09-08-calt-productivity-p5-native-unlock-accounting-design.md).

**Owner intent (2026-09-12):** Unlock qualification, SoftLand policy mutation, and productive scoring for Productivity belong in C++. Study keeps content (GRE/Notes/Math/…). Keeping `distraction_gate` as SoftLand truth is wrong; keeping `softland_policy.py` as if it were the decide brain is wrong — it is migrate/compat only.
