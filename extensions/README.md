# Focus browser extensions

| Extension | Path | Role |
|-----------|------|------|
| CALT Gate | `calt-focus/extensions/calt-gate-extension/` | SoftLand decide + DNR + locked page |
| SelfTracker | `calt-focus/extensions/selftracker-extension/` | Tab track via native `track_tab` |

Load unpacked from Edge `edge://extensions`.

## Native host (required)

```powershell
powershell -File scripts\desktop_tracker\install\install_calt_msg_host.ps1 -ExtensionIds @('<GATE_ID>','<SELFTRACKER_ID>')
```

| Message | Who | Purpose |
|---------|-----|---------|
| `get_mode` | Gate | SoftLand allow/block per URL |
| `get_softland_doc` | Gate + SelfTracker | Mode / DNR snapshot |
| `track_tab` | SelfTracker | Browser session rows in `productivity.db` |
| `ping` | Gate (health) | Host alive check |

Study `:8000` is **not** required. Optional debug: `caltGateHttpFallback=true` in extension storage.

## Rebuild after JS edits

```bat
powershell -File scripts\build_extension_workers.ps1
```

Edit `background.js` / `gate_policy.js` / `telemetry.js` — **not** `service_worker.js` (generated).
