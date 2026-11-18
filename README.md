<p align="center">
  <img src="docs/examples/calt_focus_title.png" alt="CALT Focus" width="720" />
</p>

**Your day, on purpose.**

A Windows app that helps you start with Scripture, plan the day, stay on task, and unlock free time when you’ve earned it — all on your PC, no cloud account required.

<p align="center">
  <img src="docs/examples/infographic_august_month.svg" alt="A month on CALT Focus — August 2026" width="900" />
</p>

---

## In one minute

Most tools either block websites **or** kill apps. CALT Focus does both as **two separate switches**, then wraps them in a simple daily rhythm:

1. **Bible** — morning, then afternoon (from noon), then evening (from 5pm)  
2. **Plan** — confirm today’s calendar (at least one planned minute)  
3. **Work** — start a focus session with an Allow list  
4. **Unlock** — hit your goal, claim a reward day, or spend a day pass  

Until today’s Bible slots and plan are done, Focus keeps you on those screens and site blocking stays strict. After you confirm the plan, site block and app Arm can run together — still as **separate** controls.

**Site block ON does not Arm apps.** A wrong blocked page is a refresh away; a wrong kill is not. That split is the product.

---

## A typical day

```text
Wake up
  → Site block on · Arm off
  → Morning Bible → Confirm today’s plan
  → Site block + Arm couple for the day
  → Afternoon Bible · Evening Bible when those hours arrive
  → Hit your focus goal or chores → free time / reward / pass
  → Bedtime · type EMERGENCY only if you need a wind-down
```

| How you unlock | What you get |
|----------------|--------------|
| **Daily focus goal** | Free browsing until end of day |
| **Check unlock** | +60 minutes once when chores + plan progress **and** daily goal are met |
| **Reward day** | A full free day after a qualifying streak (type `REWARD`) |
| **Day pass** | A limited weekly pass (type `PASS`) |
| **Game sleeper** | Low-CPU Focus while you game (unload attention models; SoftLand/Arm unchanged) |

Planning stays on. Yesterday’s plan does not roll into today.

```mermaid
flowchart TD
  A[Bible] --> B[Confirm plan]
  B -->|work day| C[Work]
  B -->|free day / pass / reward| G[Enjoy · rest]
  C --> D{Goal met?}
  D -->|not yet| C
  D -->|yes| E[Free mode]
  E --> F[Free time]
  F -.->|still free| F
  G --> H[Bedtime]
  F --> H
```

---

## Features (with screenshots)

### Overview — two switches, one day

Site block (browser) and Arm (OS kills) never share a switch. Overview shows what’s live and routes you to the right editor.

![Settings Overview](docs/screenshots/01-overview.png)

### Blocks · Sites — SoftLand lists

Allow / Focus / Always rules for Edge. Study mode can block YouTube & watch list; free mode opens them after you’ve earned unlock. Adult filter can stay on in free.

![Blocks · Sites](docs/screenshots/02-blocks-sites.png)

### Blocks · Apps — Arm kill list

Desktop apps on the kill list. Arm is its own switch under Unlock — SoftLand ON does not start kills.

![Blocks · Apps](docs/screenshots/03-blocks-apps.png)

### Blocks · Filters — host / device rules

Optional host-file style filters for stubborn destinations.

![Blocks · Filters](docs/screenshots/04-blocks-filters.png)

### Unlock — goal, reward, pass, Arm

Hit the daily focus goal after Bible + Confirm for free until midnight — or spend a reward day / day pass. Arm lives here as a separate control.

![Unlock](docs/screenshots/05-unlock.png)

### Day — body care + watch

Sit/stand + eye rest run locally. Opt-in attention reader / posture coach uses webcam (or MediaPipe sidecar). Amazfit/Zepp sync prefers ZeppBridge.

![Day · Body & watch](docs/screenshots/06-day-body.png)

### Gate — tab-local SoftLand lock

Edge extension redirects distraction tabs. Progress, time left to unlock, and shortcuts stay on the locked page — other tabs untouched.

![Gate · YouTube blocked in STUDY](docs/screenshots/09-gate-youtube-blocked.png)

### Data & Advanced

Export/import SoftLand + productivity mirrors, update UI/stack, Appearance, optional OpenJarvis / Telegram.

![Data](docs/screenshots/07-data.png)

![Advanced](docs/screenshots/08-advanced.png)

### Game sleeper (gaming CPU)

On Unlock: **Sleep for gaming** unloads FaceTracker/Human.js, slows status polls, and can hide Focus to the tray. SoftLand and Arm stay as you left them — this is low-CPU Focus, not SoftLand free.

---

## Two blockers — keep them separate

| | **Site block** | **Arm** |
|--|----------------|---------|
| Stops | Websites in the browser | Desktop apps |
| Good for | YouTube, rabbit holes, distraction sites | Games, chat apps on the kill list |
| Cannot | Kill Steam | Block a URL |
| Mode-aware | Yes — schedules, free windows, incubation | Yes — free time pauses kills |

**Device lock** is an optional hard lock on Windows — separate from Allow lists and free windows.

Deep rules: [`docs/BLOCKING_RULES.md`](docs/BLOCKING_RULES.md).

---

## What you get (map)

| Area | Highlights |
|------|------------|
| **Home** | Day glance, Confirm plan, work session Allow + Start/End, unlock check |
| **End of day** | Plan vs actual + sleep + sit/stand wrap · **Tomorrow’s one fix** · Close-the-day seal |
| **Calendar & Plan** | Day/week/month, drag blocks, routines, sleep windows, actuals overlay |
| **Bible & Journal** | Reader + morning/afternoon/evening devotion · mark done · journal autosave |
| **Body care** | Sit/stand + eye rest + optional webcam posture; MediaPipe sidecar + in-app recalibrate |
| **Game sleeper** | Low-CPU Focus while gaming — models off, slow polls, optional tray hide |
| **Settings** | Overview · Blocks · Unlock · Day · Appearance · Profile · Data · Advanced |
| **Gate** | Edge extension for site decisions + tab time |
| **Health** | Amazfit/Zepp sync → sleep, steps, HR · body-care day card |
| **Tray** | Rebuild / Focus / Quit — Reload UI after UI updates |

Typed confirms in Settings: `UNLOCK` · `REWARD` · `PASS` · `DEVICE LOCK`.

---

## A month in the life

Real numbers from this PC — **August 2026** desktop tracking, plus **October** watch sleep after Health sync. Titles stripped; categories kept. Raw files: [`docs/examples/`](docs/examples/).

### August · time on the PC

| | |
|--|--|
| **245 hours** across **21** days | **10,490** sessions |
| **~149h** focus work | study · code · coursework · AI |
| **~47h** play / video | games · YouTube · streaming |
| Peak hours | late morning **10–11** (also strong **8–9**) |
| Rewards | 5 qualifying August days in the ledger |

![August categories](docs/examples/infographic_august_mix.svg)

![August daily focus](docs/examples/infographic_august_focus_days.svg)

### Sleep · watch → PC

Once the watch syncs, Health shows nights like this (October sample):

![October sleep](docs/examples/infographic_october_sleep.svg)

| Night | Sleep | Steps |
|-------|------:|------:|
| Oct 9 | 6.4h | 708 |
| Oct 10 | 8.4h | 437 |
| Oct 11 | 9.7h | 539 |
| Oct 12 | 6.6h | 676 |
| Oct 14–15 | 7.5h | 2000 |

### Export your own

**Settings → More → Export** writes JSON + CSV under `data/productivity/behavior/`.

Demo copies: [`august_2026_sample_export.json`](docs/examples/august_2026_sample_export.json) · [`august_2026_day_metrics.csv`](docs/examples/august_2026_day_metrics.csv)

---

## Install (Windows)

```bat
scripts\build\build_native_enforcer.bat
powershell -File scripts\install\install_native_enforcer.ps1
```

```bat
npm run build:focus
scripts\run\run_calt_desktop.bat
```

Load the Gate extension from `extensions/calt-gate-extension` in Edge (unpacked). Use the tray to open Focus and **Reload UI** after UI updates.

Watch setup: [`docs/WEARABLES.md`](docs/WEARABLES.md).  
Body care sidecar: [`docs/BODY_CARE.md`](docs/BODY_CARE.md) · `scripts\run\run_bodycare_sidecar.bat`

---

## How it fits together

```text
CALT Focus app          Window + tray (WebView UI)
        │ reads           local day / plan / status files
        │ writes          commands to the enforcer
        ▼
Enforcer service        Site rules · Arm kills · day loop · tracking
        ▲
Gate (Edge)      ────── soft decisions + tab tracking

Sync host (optional)    Watch / Health ingest on the LAN
Body-care sidecar       MediaPipe → body_care.json (opt-in webcam)
```

Everything important lives on disk: SQLite + small JSON mirrors. No account required. SoftLand never couples to attention scores.

---

## Docs

| Doc | What |
|-----|------|
| [FOCUS_PRODUCT.md](docs/FOCUS_PRODUCT.md) | Product lock / owns |
| [BLOCKING_RULES.md](docs/BLOCKING_RULES.md) | Exact site / Arm / unlock rules |
| [SETTINGS.md](docs/SETTINGS.md) | Every Settings control |
| [BODY_CARE.md](docs/BODY_CARE.md) | Sit/stand, eye rest, sidecar, recalibrate |
| [FE_FLOWS.md](docs/FE_FLOWS.md) | Screen flows |
| [WEARABLES.md](docs/WEARABLES.md) | Watch → PC |
| [screenshots/](docs/screenshots/) | Product surface captures |
| [examples/](docs/examples/) | Sample export + charts |
| [AGENTS.md](AGENTS.md) | Contributor notes |
