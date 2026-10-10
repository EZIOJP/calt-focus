# Focus wearables — CALT Sync (Amazfit)

## Path (primary)

```text
T-Rex CALT Sync  --BLE-->  Phone Zepp side service  --HTTP-->  CALT Focus :8765
                                                                    │
                                                                    ▼
                              calt_enforcer stores wearable_*.json + life_days/
```

The watch cannot open the enforcer pipe. Focus accepts the phone POST (`/api/wearables/zepp`) and the enforcer writes the dump. `last_received_at` is the enforcer clock (UTC) when the file is stored. It stays null until a dump actually arrives.

Do not start the old Python hub. If something else already holds port 8765, Focus leaves it alone and says so from the tray — those dumps and photos never reach the enforcer.

## Setup

1. PC: open **CALT Focus** (it listens on `:8765`). Firewall once: `scripts\run\open_firewall_hub_8765.bat` as Administrator.
2. Sideload [`packages/calt-zepp`](../packages/calt-zepp) **4.3.3** (`sideload.bat` in that folder).
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

CALT Focus serves the phone page and the meal APIs on the same listener. Set one of:

- env `GEMINI_API_KEY`, `LLM_CLOUD_API_KEY`, or `LLM_API_KEY` for the Focus process
- `data/productivity/behavior/nutrition/nutrition_llm.json` → `{"gemini_api_key":"..."}` (do not commit)

Optional model override: `NUTRITION_VISION_MODEL` (default `gemini-2.0-flash`).

Flow: open `/n` → Take photo → pick detected food → edit weight (g) → Add to today. Photo recognize uses Gemini. Meal macros use the built-in table. The local Qwen model is the coach at `/assistant`, not NutriNode. See [`LOCAL_BRAIN.md`](LOCAL_BRAIN.md).

## Phone camera

Focus binds `0.0.0.0:8765` and serves the NutriNode page itself.

| URL | Use |
|-----|-----|
| `http://<PC-LAN-IP>:8765/n` | Phone **Take photo** (camera file picker). Same port as watch dumps. |
| `http://127.0.0.1:8765/n` | Windows Chrome **Start webcam** (localhost is a secure page). |
| `GET/POST /api/nutrition/*` | Today, meals, delete, photo recognize. No wearable token. |

Chrome blocks a live webcam on plain `http://LAN-IP`. The phone camera button still works there.

1. Once: `scripts\run\open_firewall_hub_8765.bat` **as Administrator** (opens 8765).
2. Open **CALT Focus**. Do not start the old Python hub — it would take `:8765`.
3. **Phone:** `http://<PC-LAN-IP>:8765/n` → **Take photo**. Optional: Add to Home screen.
4. **This PC:** `http://127.0.0.1:8765/n` → **Start webcam** → Capture.

Meals land in `behavior/nutrition/days/` and `behavior/nutrition_today.json`. Photo recognize calls Gemini from Focus (`winhttp`). SoftLand/Arm stay on the desktop enforcer.

## Optional

Google Fit / Health Connect shaped dumps (`source: google_fit`) are stored by the same enforcer ingest.
