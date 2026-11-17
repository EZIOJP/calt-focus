# Body care — sit/stand, eye rest, vision sidecar

**SoftLand ≠ Arm.** Body care never arms SoftLand or the kill list.

## Pieces

| Piece | Owner |
|-------|--------|
| Sit / stand + eye-rest nudges | Focus WebView `BodyCareRuntime` (prefs in Settings → Day) |
| Human.js attention/posture fallback | Opt-in webcam in WebView (`BodyCareVisionHost`) |
| **Vision sidecar** (preferred math) | Python MediaPipe process → behavior mirrors |

## Sidecar (Windows)

```bat
scripts\run\run_bodycare_sidecar.bat
scripts\run\run_bodycare_calibrate.bat          REM ~12s neutral gaze + preview
scripts\run\run_bodycare_calibrate_sit.bat      REM ~4s sitting upper-body baseline
scripts\run\run_bodycare_calibrate_stand.bat    REM ~4s standing upper-body baseline
```

Requires Python 3.11+ (creates `.venv` under `scripts\run\bodycare_sidecar\`).

**In-app recalibrate** (Settings → Day): with sidecar running, buttons POST to  
`http://127.0.0.1:8766/calibrate` `{ "mode": "neutral" | "sit" | "stand" }`.  
Also `GET /health`. Port is mirrored in `body_care.json` → `control.port`.

Writes:

- `data/productivity/behavior/body_care.json` — latest sample + day summary + `control`  
- `data/productivity/behavior/body_care_days/YYYY-MM-DD.json` — daily rollup  
- `data/productivity/behavior/body_care_calibration.json` — neutral + sit/stand baselines  

Sample fields: `attention_pct`, `gaze_strength`, `blink_rate` (blinks/min), `blink_just_now`, `ear`, `yaw/pitch/roll`, `neck_angle`, `torso_angle`, `posture_score`, `distance_proxy`, `sit_stand_hint`, `sit_stand_score`, `nose_y`, `shoulder_y`, `ipd`, `present`, `calibrating`.

Blink uses eye-aspect-ratio rising edges vs calibrated `ear0` (from Gaze recalibrate). Shown on Settings → Day live strip and Health body-care card.

### Sit / stand (laptop webcam)

No real ToF / depth camera — monocular cues only:

1. **Primary:** calibrated nose / shoulder **Y** (standing shifts landmarks upward in frame).  
2. **Secondary:** face width + inter-pupillary distance (often shrinks when standing farther).  
3. **Optional:** hip–shoulder ratio only when hips are actually visible.  

Run **sit** then **stand** calibrate once (same desk / screen angle). Until both ready, hint stays `unknown` (or weak hip fallback). Frames stay in RAM; only landmarks + state are mirrored. `BodyCareRuntime` freezes the sit clock when `sit_stand_hint=stand`.

Algorithms inspired by eyetrax (EMA + calibration), ErgoGuard / PosturePro (neck/torso), gaze-face trackers (iris/EAR). **Not** ActivityWatch (enforcer already tracks apps).

## App surfaces

- **Settings → Day** — prefs + live sidecar strip + sit/stand calibrate hint  
- **Health / Life** — `BodyCareDayCard` (attention / posture / present / slouch / sit·stand hint mins)  
- **Export** — `health_days` columns `body_*` + `has_body_care`  

## Product locks

- Optional Python **body-care only** — still zero Python for SoftLand / arm / kill / status  
- Camera off by default; SoftLand never couples to attention scores  
