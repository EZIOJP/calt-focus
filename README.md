# CALT Focus

Local-first **Productivity** app (SoftLand + Arm + Plan/Calendar + Bible/Journal).

This repository is **standalone**. It does not require the Study (GRE/notes) project or Python `:8000` for SoftLand / Arm / tracking.

## Layout

```text
backend/          calt_enforcer · calt_focus · calt_msg_host
extensions/       Gate + SelfTracker
frontend/         Thin notes + optional Vite entry (UI built in Study → dist-focus)
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

UI: React Focus SPA from Study sibling → `dist-focus/` (`npm run build:focus` or `sync:focus-ui`). Tray **Reload UI** after sync.

## Naming

| Flag | Meaning |
|------|---------|
| `softland_enabled` | Site SoftLand only |
| `hard_block_armed` | OS kill list |

SoftLand ON ≠ Arm.

## Docs

- [docs/BLOCKING_RULES.md](docs/BLOCKING_RULES.md)
- [Separate-repo extract](docs/superpowers/specs/2026-10-03-calt-focus-separate-repo-design.md)
