# CALT Focus → separate folder + repo (2026-10-03)

**Status:** Design — await owner approval before extract  
**Pairs with:** [hybrid HTML shell](./2026-10-03-calt-focus-hybrid-html-shell-design.md)

## Goal

Own **CALT Focus** (Productivity) as its own product tree and git repo so Study React/Node no longer sits in the same mental (or CI) bag. Hybrid HTML FE lives there; C++ enforcer/host stay with Focus.

## Target layout (new repo)

Suggested name: **`calt-focus`** (or `CALT-Focus`).

```text
calt-focus/                          ← git root
  README.md
  AGENTS.md                          ← productivity-only rules
  frontend/
    shell/                           ← HTML/CSS/JS (hybrid; primary)
    (legacy react/ optional until cut)
  backend/
    calt_enforcer/
    calt_focus/
    calt_msg_host/
  extensions/
    calt-gate-extension/
    selftracker-extension/
  scripts/
    build/ install/ run/             ← from scripts/desktop_tracker
  data/
    productivity/                    ← SoT + mirrors + bible (or %ProgramData% later)
  docs/
    BLOCKING_RULES.md
    superpowers/specs|plans (Focus-only)
  dist-focus/                        ← build output (gitignored or release artifact)
```

**Default data path on machine (runtime):** keep `C:\ProgramData\CALT\…` or repo-relative `data/productivity` as today — do not invent a third SoT in the extract.

## What stays in Study repo (`Cognitive-Aware Learning Tutor`)

| Keep | Why |
|------|-----|
| `src/` Study app, GRE, notes, math, plugins | Study product |
| `backend/` FastAPI vocab / study APIs | Study `:8000` |
| `data/vocab_app.db` | Study content |
| Study docs / run.bat for webapp | Study door |

**After extract:** Study `/productivity*` stays interstitial → “open Focus”; no SoftLand brain; optional thin shims only.

## What moves (from current monorepo)

| Move | Notes |
|------|--------|
| `calt-focus/**` | Already the product root |
| `scripts/desktop_tracker/**` | Build/install/run for enforcer/msg-host |
| `data/productivity/**` | DB + behavior mirrors + bible (careful: local state) |
| Focus-relevant specs/plans under `docs/superpowers/**` | Copy or git-filter; leave Study docs behind |
| `docs/BLOCKING_RULES.md` | Belongs with Focus |
| Hybrid shell work | New under `frontend/shell/` |

**Do not move:** Study `package.json` / Vite Study entry — Focus shell should not depend on Study `node_modules` once HTML FE lands. Until hybrid Phase 0 ships, a short **git submodule / sibling path** to shared React is OK, then delete.

## Extract sequence (safe)

1. **Approve this doc** + hybrid Phase 0  
2. Create sibling folder / new git repo (empty)  
3. Copy (not delete yet) Focus trees + fix absolute paths in CMake/bats/ps1 that assume Study repo root  
4. Point `CALT_DB` / enforcer install at the same live `data/productivity` **or** migrate once with a backup  
5. Smoke: enforcer + Focus + Gate with Study repo closed / `:8000` stopped  
6. In Study repo: leave `calt-focus/` as a **stub README** (“moved to …”) or git submodule — owner choice  
7. Hybrid HTML FE proceeds **only in the Focus repo**

## Path fixes expected

- `calt_focus` WebView `dist-focus` / `calt-data.app` roots  
- `build_native_enforcer.bat` / install scripts (`$Repo` parents)  
- msg-host SoftLand policy path relative to install dir  
- Any `Join-Path $Repo "data\productivity"` assumptions

## Out of scope

- Merging Study into Focus  
- Electron  
- Rewriting enforcer in another language  
- Moving `vocab_app.db`

## Approval

Owner confirms: **repo name**, **sibling path** (e.g. `Desktop/calt-focus`), and whether Study keeps a stub or submodule — then extract runs.
