# Focus wearables — ZeppBridge (preferred) + CALT Sync

## Preferred path — [ZeppBridge](https://github.com/lingcang728/ZeppBridge)

```text
Amazfit  -->  Zepp phone cloud  -->  ZeppBridge desktop (local zepp.db)
                                         │
                                         ▼  scripts/run/pull_zeppbridge.py
         wearable_day.json + wearable_days/ + life_days/ + sleep_windows.json
         + zeppbridge_collector/{YYYY-MM-DD}.json   (rich archive)
```

| Setting | Value |
|---------|--------|
| **App** | ZeppBridge (Local REST ON) |
| **API** | `http://127.0.0.1:43921` (`/health`, `/workouts/{id}/series`) |
| **Token** | Settings → Local REST → Bearer `zbk_…` (stored in `behavior/zeppbridge.json`, gitignored) |
| **Day health** | Read-only `…/ZeppBridge/data/zepp.db` (REST does not expose daily sleep/steps) |
| **Pull** | Settings → Watch ↔ PC → **Pull from ZeppBridge**, or `python scripts\run\pull_zeppbridge.py` |

Keep ZeppBridge open and synced; then pull into Focus. CALT Sync below stays as a fallback.

### Collector format (schema 3)

`pull_zeppbridge.py` writes **payload.schema = 3** with a nested `collector` block that mirrors ZeppBridge’s daily metrics (same keys as ZeppBridge AI exports). Empty cells stay **null** (not 0).

| Path | Role |
|------|------|
| `behavior/wearable_day.json` | Today summary (FE-compat top-level + `payload.collector`) |
| `behavior/wearable_days/{day}.json` | Per-day same shape |
| `behavior/zeppbridge_collector/{day}.json` | Rich archive only (`calt.zeppbridge.collector` v1) |
| `behavior/life_today.json` / `life_days/` | Life Tracker summary (`schema_version` 2 + readiness / HRV / charges) |

`collector` fields: `coverage`, `daily` (readiness, charges, stress min/max, sleep_hrv, training_load, PAI, …), `metrics_all` (every `daily_metrics` key), `sleep`, `workouts`, `samples` (downsampled HR / HRV / HRV_RMSSD / stress / SpO₂ from `metric_samples`). Health → Watch shows these as vitals charts.

Promoted onto the wearable summary for Health UI: `readiness`, `training_load`, `hybrid_charge`, `physical_charge`, `mental_charge`, `sleep_hrv`, `stress_min`, `stress_max`, `active_minutes`.

## Legacy path — CALT Sync (Amazfit)

```text
T-Rex CALT Sync  --BLE-->  Phone Zepp side service  --HTTP-->  calt_sync_host :8765
                                                                  │
                                                                  ▼
                                       wearable_*.json + life_days/ + Health UI
```

| Setting | Value |
|---------|--------|
| **Receiver** | `calt_sync_host.exe` (C++) — **not** Python, not the enforcer |
| **Base URL** | `http://192.168.0.110:8765` (this PC’s Wi‑Fi LAN IP; update if DHCP changes) |
| **Token** | `calt-local-wearables` |
| **Ingest** | `POST /api/wearables/zepp` (+ `/raw`, `/ping`) |
| **Health check** | Phone browser → `http://192.168.0.110:8765/health` |

Never use `http://127.0.0.1:8765` from the phone/watch — that is the phone itself.

Live mirror of the URL: `data/productivity/behavior/calt_sync_url.json` (written whenever the receiver starts).

## Setup (once)

1. Admin once: `scripts\run\open_firewall_hub_8765.bat`
2. Start + keep alive: `scripts\run\start_calt_sync_host.bat`  
   Or install logon task:  
   `powershell -File scripts\run\ensure_calt_sync_host.ps1 -InstallTask`
3. Sideload watch app: [`packages/calt-zepp`](../packages/calt-zepp) **4.4+**
4. Phone Zepp → CALT Sync settings → Base URL + token from the table above
5. Watch: **Dump & Send** once; enable **Auto sync** if you want recurring dumps

`ensure_calt_sync_host.ps1` clears port conflicts (e.g. `python -m http.server 8765`) before starting the C++ receiver with an exclusive bind.

## Do not use for watch ingest

- `scripts\run\start_wearables_hub.bat` / `wearables_hub.py` — deprecated for Amazfit; fights `:8765`
- SoftLand/Arm enforcer — different process, different job

## Focus UI

- Settings → Watch ↔ PC — hub status
- Sidebar **Health** — Life Tracker from last dump
