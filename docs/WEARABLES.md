# Focus wearables — CALT Sync (Amazfit)

## Path (primary)

```text
T-Rex CALT Sync  --BLE-->  Phone Zepp side service  --HTTP-->  Focus hub :8765
                                                                    │
                                                                    ▼
                                         wearable_*.json + life_days/ + Health UI
```

Same dump/send logic as Study. No Google Fit middleware required.

## Setup

1. PC: `scripts\run\start_wearables_hub.bat`
2. Sideload Focus SoT watch app: [`packages/calt-zepp`](../packages/calt-zepp) **4.3+** (`sideload.bat` in that folder)
3. Phone Zepp → CALT Sync settings → Base URL `http://<PC-LAN-IP>:8765`, token `calt-local-wearables`
4. Watch: **Dump & Send** once to verify. Open `http://<PC-LAN-IP>:8765/health` — `last_received_at` stays `null` until a dump actually arrives, then it is the hub clock (UTC). The watch home line reads **PC received: never** until that stamp comes back.
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
| `8765` HTTP | Amazfit sync + phone **Take photo** (file capture) |
| `8766` HTTPS | **Webcam** in Windows/phone Chrome (`getUserMedia`) |

Chrome blocks the live webcam on plain `http://LAN-IP`. Use HTTPS (self-signed; Advanced → Proceed once) or `http://127.0.0.1:8765/n` on the PC.

1. Once: `scripts\run\open_firewall_hub_8765.bat` **as Administrator** (opens 8765 + 8766)
2. Keep hub running (`start_wearables_hub.bat`)
3. **Windows Chrome:** `https://127.0.0.1:8766/n` → **Start webcam** → Capture
4. **Phone:** `http://<PC-LAN-IP>:8765/n` → **Take photo**, or HTTPS `:8766/n` for live camera
5. Optional: Add to Home screen

SoftLand/Arm stay on the desktop enforcer; this LAN surface is NutriNode (+ wearables ingest).

## Optional

Google Fit / Health Connect phone sync still accepted by the hub (`source: google_fit`) if you want a backup path.
