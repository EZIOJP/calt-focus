# CALT Sync 4.3.3 — rich health dumper (Zepp OS 6)

Watch app that dumps **rich body metrics** to the PC. The only receiver is CALT Focus on `:8765`. The enforcer stores the dump. There is no Study `:8000` fallback.

Watch stamps **calendar day + timezone offset** before BLE. Phone must not replace that with its own clock.

## What it does

| Action | Behavior |
|--------|----------|
| **Dump today** | Capture full body snapshot into the local queue |
| **Dump & Send** | Capture then flush queue in one tap |
| **Send queue** | Upload queued days to CALT Focus `:8765` |
| **Auto sync** | Watch Settings → ON · alarm every 1/3/6/12h → Dump & Send |
| **Test PC** | Settings → Test PC pings CALT Focus `:8765/health` |

## Rich payload (`processed_v2`)

When sensors exist: sleep (+ stages / light / rem / naps), HR (+ downsampled today series, max/avg), resting HR, stress (+ hour/week), SpO₂ (+ recent), steps, calories, distance, PAI, stand, fat burn, sitting, battery, temperature, **workouts**, **HR zones**, weather/device meta.

Chunks (5): Sleep · Activity · Heart · Series · Extras — resume-safe, idempotent.

## PC setup

1. Open **CALT Focus**. It listens on `:8765` for the whole LAN. Quit the Python hub first if that process already holds the port.
2. Phone Zepp → **CALT Sync** settings:
   - **PC address:** `http://<PC-LAN-IP>:8765`
   - **Ingest token:** `calt-local-wearables`
3. Phone browser: `http://<IP>:8765/health` — `store` is `calt_enforcer`. `last_received_at` stays null until a dump arrives.

## Install

```bat
packages\calt-zepp\sideload.bat
```

Uninstall older CALT Sync first, then sideload **4.3.3**. Turn on **Auto sync** in watch Settings after the first successful Dump & Send.

## Hub / API

```text
POST /api/wearables/zepp
GET  /api/wearables/zepp/health
GET  /api/wearables/zepp/status   → categories inventory
```

Those routes are served by CALT Focus. Auth: `Authorization: Bearer <token>` + `X-CALT-Wearable-Key`.

## Limits

- No historical days the app never dumped
- BLE transfers are slow — keep watch + phone awake during Send
- Workout history only if firmware exposes `Workout` API
