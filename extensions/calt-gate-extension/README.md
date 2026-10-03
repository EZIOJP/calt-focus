# CALT Gate (blocker) — v1.0.8

Browser **SoftLand** (DNR + locked page) for CALT Focus. Talks to **`calt_msg_host.exe`** (C++) only by default.

## Pair with

- `selftracker-extension` **v1.5.28+** (telemetry → `track_tab`)
- **`calt_enforcer.exe`** — SoftLand SoT + Arm kills
- **`calt_focus.exe`** — Settings / Plan UI

## Load in Edge

1. `edge://extensions` → Developer mode  
2. Load unpacked → `calt-gate-extension`  
3. Load unpacked → `selftracker-extension`  
4. Register host with **both** extension IDs:

```powershell
powershell -File scripts\desktop_tracker\install\install_calt_msg_host.ps1 -ExtensionIds @('<GATE_ID>','<SELFTRACKER_ID>')
```

After editing JS:

```bat
powershell -File scripts\build_extension_workers.ps1
```

Then **Reload** both extensions.

## Requires (native path)

- `calt_msg_host.exe` — `get_mode` (per navigation) + `get_softland_doc` (DNR / mode cache)
- `calt_enforcer.exe` running — owns `softland_policy.json` / SQLite
- Study `:8000` is **not** required

## Optional Study debug

Set storage `caltGateHttpFallback=true` (alias `caltSoftlandHttpFallback`) to enable:

- HTTP `distraction-gate`
- Gate notify WebSocket
- Alert / extension-log POSTs

## Stability (v1.0.8)

- Reused native messaging port + short `get_mode` cache
- Temp-allow honored before SoftLand redirect
- Locked page reads SW `GET_GATE` (no `:8000` poll)
- Single alarm-based policy poll (no dual setInterval storm)
