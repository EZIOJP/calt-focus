# CALT Native Messaging Host — SoftLand get_mode + track_tab

Original CALT code. SoftLand decide reads `data/behavior/softland_policy.json` in C++.
Browser track writes `tracked_sessions` (WAL + busy_timeout). No Python required for either.

See: `docs/superpowers/specs/2026-09-08-calt-productivity-solo-pack-design.md`

## Build

```bat
scripts\desktop_tracker\build_calt_msg_host.bat
```

Output: `native\calt_msg_host\build\Release\calt_msg_host.exe` (or `build\`).

## Protocol

- Input: 4-byte LE length + UTF-8 JSON
- `{"type":"ping"}` → `{"type":"pong","ok":true,"host":"calt_msg_host","softland":true,"track":true}`
- `{"type":"get_mode","schema_version":1,"url":"https://…"}` → SoftLand decide JSON (`mode`, `reason`, `until`, …)
- `{"type":"track_tab","url":"…","title":"…","domain":"…"}` → `{ok:true}` session row (`source=selftracker`)

Missing SoftLand policy → **fail-closed** block (`reason=softland_policy_missing`).

## Register (Gate + SelfTracker)

```powershell
powershell -File scripts\desktop_tracker\install\install_calt_msg_host.ps1 -ExtensionIds @('<GATE_ID>','<SELFTRACKER_ID>')
```

Reload both extensions (`nativeMessaging` on Gate + SelfTracker).

## HTTP fallback

Gate SoftLand: native first, HTTP `:8000` if host missing.  
SelfTracker telemetry: native `track_tab` first, HTTP `browser-telemetry` fallback.
