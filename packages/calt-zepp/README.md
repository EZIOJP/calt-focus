# CALT Sync 4.4 — rich health dumper (Zepp OS 6)

Watch app that dumps **rich body metrics** to the PC. Posts only to **`calt_sync_host` on `:8765`**.

| Setting | Final value |
|---------|-------------|
| Base URL | `http://192.168.0.110:8765` |
| Token | `calt-local-wearables` |

Never use localhost from the phone. Watch stamps **calendar day + timezone offset** before BLE.

## What it does

| Action | Behavior |
|--------|----------|
| **Sync** | One button on a black screen. Stays awake and sends every queued day through now, one chunk at a time |
| **Auto sync** | Swipe → Auto / interval. Same send |
| **Test PC** | Settings → same as Ping PC (health + notify) |

## Rich payload (`processed_v2`)

When sensors exist: sleep (+ stages / light / rem / naps), HR (+ downsampled today series, max/avg), resting HR, stress (+ hour/week), SpO₂ (+ recent), steps, calories, distance, PAI, stand, fat burn, sitting, battery, temperature, **workouts**, **HR zones**, weather/device meta.

Chunks (5): Sleep · Activity · Heart · Series · Extras — resume-safe, idempotent.

## PC setup

1. `scripts\run\start_calt_sync_host.bat` (exclusive C++ receiver; clears port fights).
2. Optional Admin once: `open_firewall_hub_8765.bat` + `ensure_calt_sync_host.ps1 -InstallTask`.
3. Phone Zepp → **CALT Sync** settings → Base URL + token from the table above.
4. Phone browser: `http://192.168.0.110:8765/health`

## Install

```bat
packages\calt-zepp\sideload.bat
```

Uninstall older CALT Sync first, then sideload **4.4.0**. The watch face is one Sync button. Swipe for Auto / interval.

## Hub / API

```text
POST /api/wearables/zepp          → ingest (Focus coerces + mirrors; stores wearable_raw/)
POST /api/wearables/zepp/raw      → raw dump; Focus does all processing
POST /api/wearables/zepp/ping     → connectivity ping → PC toast + wearables_ping.json
GET  /api/wearables/zepp/health
GET  /api/wearables/zepp/status   → categories inventory + last_ping
```

Auth: `Authorization: Bearer <token>` + `X-CALT-Wearable-Key`.

## Limits

- No historical days the app never dumped
- BLE transfers are slow — keep watch + phone awake during Send
- Workout history only if firmware exposes `Workout` API
