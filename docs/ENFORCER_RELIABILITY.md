# CALT Enforcer Reliability

This document is the single reference for how CALT Focus policy reaches the
native enforcer, what has malfunctioned, and the direction for reliable
repair. It describes the Focus product only; it is not a Study `:8000`
fallback design.

## Current architecture

```text
Focus UI / tray
  │  native named-pipe commands
  ▼
CALT native enforcer (Windows service)
  ├─ loads policy and applies app/site/gate rules
  ├─ owns the `calt_enforcer_cmd` pipe
  ├─ runs the foreground tracker through a user-session agent
  └─ publishes behavior mirrors and status
       │
       ├─ SQLite durable state: data/productivity/productivity.db
       └─ behavior mirrors: data/productivity/behavior/*.json
```

The UI should be a control surface, not the process that keeps restrictions
alive. The enforcer must continue applying the already-saved policy if Focus
is closed, updating, or crashes. Conversely, the enforcer must not repeatedly
relaunch Focus from its Session-0 service process.

## Current policy locations

The durable source of truth is:

```text
data/productivity/productivity.db
```

The enforcer currently also consumes and publishes mirrors below:

```text
data/productivity/behavior/
```

The relevant files are:

| File | Purpose |
|---|---|
| `enforcer_policy.json` | Arm state, lock state, blocked executables, anti-tamper options |
| `softland_policy.json` | SoftLand state, schedules, site rules, free time, incubation |
| `softland_site_rules.json` / `porn_blocklist.json` | Additional site/category rules |
| `plan_blocks.json` | Plan and gate restrictions |
| `work_session.json` | Work-session restrictions |
| `gate_heartbeat.json` | Browser Gate extension liveness signal |
| `enforcer_status.json` | Enforcer health, loaded policy state, last action |
| `enforcer_kills.log` | Historical process terminations |

At the time of investigation, Arm was off and SoftLand was on. That is valid:
SoftLand and Arm are separate locks. SoftLand is for site/gate behavior; Arm
is for executable blocking.

## Observed failures

### 1. Stale status looked live

When an enforcer process stopped or was force-killed, its most recent
`enforcer_status.json` could still say that it owned enforcement and that the
service was running. Focus could then treat a stale armed state as live.

The repair made two improvements:

- Focus only treats Arm state from a fresh heartbeat as active for recovery
  decisions.
- A graceful enforcer stop writes an explicit non-owning, stopped heartbeat.
- The status now distinguishes a healthy console fallback from the Windows
  service: a console enforcer can be live while `service_running` is false.

Force-kills cannot publish a final state, so heartbeat freshness remains the
required safety check.

### 2. Gate Watch closed all Edge processes

`gate_watch.cpp` considers the Gate extension dead after a heartbeat is older
than 180 seconds. With SoftLand enabled and no free/reward period, two dead
strikes can call the global `KillMatchingExes({"msedge.exe"})` fallback.

Evidence in `enforcer_kills.log` showed bulk Edge closures on 2026-10-20:
17 processes at 10:54, 11 at 14:19, and 26 at 18:11. The enforcer status
reported `last_kill_reason: "gate_dead"`.

This is too broad. A missing extension heartbeat can happen when the browser
extension sleeps, a browser profile lacks the extension, native messaging is
unavailable, or the extension otherwise fails. It must not immediately imply
that every Edge window should be terminated.

### 3. Focus watchdog relaunch storm

The enforcer status reported 293 Focus relaunches during one service lifetime.
`focus_watchdog.cpp` is called from the Windows service (Session 0) but uses
`CreateProcessW` directly to launch `calt_focus.exe`. That can create an
invisible/non-interactive Focus process rather than the user-visible app,
which then exits or fails and causes another launch attempt.

The foreground tracker avoids this mistake by starting a dedicated agent in
the interactive user session. Focus needs the same discipline, or the
enforcer should stop watchdog-relaunching it entirely.

### 4. Service deployment needs elevation

The permanent binary is installed under `C:\ProgramData\CALT\enforcer` and
requires Administrator privileges to update. A rebuilt console fallback can
enforce policy immediately, but it is not the desired boot-persistent service
deployment. The repair UI must clearly distinguish these states.

## Desired policy contract

Do not combine the Focus UI and enforcer into one executable. Doing that would
make enforcement disappear whenever WebView2 or the UI crashes.

Instead, consolidate **enforcement decisions** into one generated,
versioned manifest. Keep journals, plans, history, and other product data in
SQLite.

```text
Focus → command request → native enforcer → SQLite transaction
                                      └→ atomic enforcement manifest
                                            └→ loaded by enforcer
```

Suggested manifest:

```text
data/productivity/behavior/enforcement_state.json
```

Minimum fields:

```json
{
  "schema_version": 1,
  "revision": 1843,
  "hash": "policy-content-hash",
  "written_at": "2026-10-21T06:15:00+05:30",
  "writer": "calt_enforcer",
  "softland": {},
  "arm": {},
  "gate": {}
}
```

Rules of the contract:

1. Focus sends requested changes only through the native command gateway; it
   never writes enforcement files or enforcement rows directly.
2. Requests include the revision Focus last read. The enforcer rejects a
   stale write with `revision_conflict` rather than overwriting newer state.
3. The enforcer validates the whole candidate policy before committing.
4. SQLite writes happen in a transaction. The manifest is written to a
   temporary file and atomically replaced only after validation.
5. The enforcer swaps in-memory rules only after the durable write succeeds.
6. On any validation or I/O error, it keeps the last known-good policy and
   publishes a prominent `POLICY_ERROR` state with the exact reason.

## Self-review and health model

At startup and at a regular interval (30–60 seconds), the enforcer should
check:

- manifest parses and matches the expected schema;
- manifest revision/hash matches the SQLite policy transaction;
- policy timestamp is not unexpectedly stale;
- exactly one owner holds the named-pipe/owner mutex;
- command gateway answers a local probe;
- tracker agent belongs to the active interactive session;
- Gate state is classified as `healthy`, `grace`, `degraded`, or `failed`;
- Focus is not in a relaunch loop.

`enforcer_status.json` should expose the loaded revision, hash, policy age,
health state, last validation time, and last error—not just a boolean alive
signal.

## Gate failure policy

Gate health is not equivalent to proof that every browser window is bypassing
blocking. The desired sequence is:

```text
healthy → heartbeat late → grace → degraded warning → narrowly scoped fallback
```

The fallback must be explicit and configurable. It should not globally kill
all `msedge.exe` processes merely because one extension heartbeat is stale.
Strict mode may close a verified Gate-managed browser only after its grace
period; normal mode should display a repair prompt and retain diagnostic data.

## Repair enforcer feature

Replace the current restart-only affordance with **Repair enforcer**, exposed
in the tray and Settings → Enforcer.

Repair must:

1. Read persisted enforcement policy from SQLite.
2. Validate it and regenerate the manifest atomically.
3. Verify revision/hash agreement and record the result.
4. Remove only duplicate/rogue enforcer owners.
5. Restart the one intended service owner, or explicitly report that only a
   console fallback is available.
6. Wait for a fresh heartbeat and a successful command-pipe status response.
7. Show a concrete report in Focus.

Example success:

```text
Enforcer repaired
Policy revision: 1843
Arm: off
SoftLand: study mode
Gate: healthy
Service: running
```

Example failure:

```text
Repair needs attention
Policy revision 1843 could not be validated: invalid Gate rule
Last known-good revision 1842 remains active
```

Repair rebuilds from persisted state. It must never copy values from an open,
possibly stale Focus screen into policy.

## Design principle

Focus defines and edits the rules. The native enforcer owns the effective,
validated policy and applies it continuously. This preserves permanent
enforcement without coupling it to the WebView UI, and makes every mismatch
visible and recoverable.
