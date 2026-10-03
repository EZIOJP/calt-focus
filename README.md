# CALT Focus

Local-first **Productivity** app (SoftLand + Arm + Plan/Calendar + Bible/Journal).

This repository is **standalone**. It does not require the Study (GRE/notes) project or Python `:8000` for SoftLand / Arm / tracking.

## Layout

```text
backend/          calt_enforcer · calt_focus · calt_msg_host
extensions/       Gate + SelfTracker
frontend/shell/   HTML/CSS/JS UI (primary — hybrid design)
frontend/         Legacy Vite entry (optional; no Study src/)
scripts/          build · install · run
data/productivity SoftLand SoT + mirrors (+ bible corpus)
docs/             BLOCKING_RULES + designs
```

## Quick start

```bat
scripts\build\build_native_enforcer.bat
powershell -File scripts\install\install_native_enforcer.ps1
:: Admin once
```

```bat
npm run build:focus
scripts\run\run_calt_desktop.bat
```

Edit UI under `frontend\shell\` → `npm run build:focus` → tray **Reload UI**.

## Naming

| Flag | Meaning |
|------|---------|
| `softland_enabled` | Site SoftLand only |
| `hard_block_armed` | OS kill list |

SoftLand ON ≠ Arm.

## Docs

- [docs/BLOCKING_RULES.md](docs/BLOCKING_RULES.md)
- [Hybrid HTML shell](docs/superpowers/specs/2026-10-03-calt-focus-hybrid-html-shell-design.md)
- [Separate-repo extract](docs/superpowers/specs/2026-10-03-calt-focus-separate-repo-design.md)
