# Focus wearables — CALT Sync (Amazfit)

## Path (primary)

```text
T-Rex CALT Sync  --BLE-->  Phone Zepp side service  --HTTP-->  CALT Focus :8765
                                                                    │
                                                                    ▼
                              calt_enforcer stores wearable_*.json + life_days/
```

The watch cannot open the enforcer pipe. Focus accepts the phone POST (`/api/wearables/zepp`) and the enforcer writes the dump. `last_received_at` is the enforcer clock (UTC) when the file is stored. It stays null until a dump actually arrives.

Quit the Python hub before opening Focus. If port 8765 is already taken, Focus leaves it alone and says so from the tray — those dumps never reach the enforcer.

## Setup

1. PC: open **CALT Focus** (it listens on `:8765`). Firewall once: `scripts\run\open_firewall_hub_8765.bat` as Administrator.
2. Sideload [`packages/calt-zepp`](../packages/calt-zepp) **4.3.2** (`sideload.bat` in that folder).
3. Phone Zepp → CALT Sync settings → Base URL `http://<PC-LAN-IP>:8765`, token `calt-local-wearables`.
4. Watch: **Dump & Send** once. Open `http://<PC-LAN-IP>:8765/health`. `last_received_at` is null until that dump lands. The watch home line reads **PC received: never** until the stamp comes back.
5. Watch Settings → **Auto sync: ON** (interval default **3h**, cycle 1 / 3 / 6 / 12)

Watch package lives in this repo (`packages/calt-zepp`). Do not sideload from the Study sibling for Focus.

Auto uses a persistent watch alarm that wakes the dump page and runs Dump & Send. Phone should be nearby (BLE) and on the same Wi‑Fi as the PC.

## Focus UI

- Settings → Watch ↔ PC — hub status / test payload
- Sidebar **Health** — Life Tracker from last dump
- Sidebar **NutriNode** — meal search/log + **Photo suggest** on the same hub

### NutriNode photo AI

Same process as the wearables hub. Set one of:

- env `GEMINI_API_KEY` or `LLM_CLOUD_API_KEY` before starting the hub
- `data/productivity/behavior/nutrition/nutrition_llm.json` → `{"gemini_api_key":"..."}` (do not commit)
- sibling Study `.env` with `LLM_CLOUD_API_KEY` (auto-read for local)

Flow: Photo suggest → pick detected food → edit weight (g) → Add → Send.

## Phone / Windows Chrome NutriNode

Hub binds `0.0.0.0`:

| Port | Use |
|------|-----|
| `8765` HTTP | Amazfit sync while Focus is open. Phone **Take photo** uses this port only when the Python hub is running and Focus is quit. |
| `8766` HTTPS | **Webcam** in Windows/phone Chrome (`getUserMedia`), Python hub |

Chrome blocks the live webcam on plain `http://LAN-IP`. Use HTTPS (self-signed; Advanced → Proceed once) or `http://127.0.0.1:8765/n` on the PC.

1. Once: `scripts\run\open_firewall_hub_8765.bat` **as Administrator** (opens 8765 + 8766)
2. For NutriNode photos, quit Focus and run `start_wearables_hub.bat` (Focus owns `:8765` for watch dumps while it is open)
3. **Windows Chrome:** `https://127.0.0.1:8766/n` → **Start webcam** → Capture
4. **Phone:** `http://<PC-LAN-IP>:8765/n` → **Take photo**, or HTTPS `:8766/n` for live camera
5. Optional: Add to Home screen

SoftLand/Arm stay on the desktop enforcer. Watch dumps do too, through Focus on `:8765`.

## Optional

Google Fit / Health Connect shaped dumps (`source: google_fit`) are stored by the same enforcer ingest.
