#!/usr/bin/env python3
"""Probe Focus FE↔BE connectivity: gateway + mirrors + planner SoT."""
from __future__ import annotations

import json
import sqlite3
import subprocess
import sys
from datetime import date, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BEH = ROOT / "data" / "productivity" / "behavior"
DB = ROOT / "data" / "productivity" / "productivity.db"
GW = ROOT / "scripts" / "desktop_tracker" / "run" / "gateway_cmd.ps1"
OUT = ROOT / "docs" / "superpowers" / "exports" / "2026-09-14-focus-fe-be-connectivity-report.md"


def gateway(op: str, payload: dict | None = None) -> tuple[bool, dict | str]:
    payload = payload or {}
    tmp = ROOT / ".tmp_gw_payload.json"
    tmp.write_text(json.dumps(payload), encoding="utf-8")
    try:
        r = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-File",
                str(GW),
                "-Op",
                op,
                "-PayloadFile",
                str(tmp),
                "-TimeoutMs",
                "8000",
            ],
            capture_output=True,
            text=True,
            cwd=str(ROOT),
        )
        out = (r.stdout or "").strip() or (r.stderr or "").strip()
        try:
            i = out.find("{")
            doc = json.loads(out[i:]) if i >= 0 else {}
        except Exception:
            doc = {}
        return r.returncode == 0 and bool(doc.get("ok")), doc or out
    finally:
        try:
            tmp.unlink()
        except OSError:
            pass


def main() -> int:
    lines: list[str] = []
    lines.append("# Focus FE ↔ BE connectivity report")
    lines.append("")
    lines.append(f"**When:** {date.today().isoformat()} (machine local)")
    lines.append("")

    # Processes
    def running(name: str) -> bool:
        r = subprocess.run(
            ["tasklist", "/FI", f"IMAGENAME eq {name}"],
            capture_output=True,
            text=True,
        )
        return name.lower() in (r.stdout or "").lower()

    enf = running("calt_enforcer.exe")
    foc = running("calt_focus.exe")
    lines.append("## Processes")
    lines.append("")
    lines.append(f"| Process | Running |")
    lines.append(f"|---------|---------|")
    lines.append(f"| calt_enforcer.exe | {'YES' if enf else 'NO'} |")
    lines.append(f"| calt_focus.exe | {'YES' if foc else 'NO'} |")
    lines.append("")

    # Mirrors
    lines.append("## Data mirrors (`calt-data.app` → `data/productivity/behavior`)")
    lines.append("")
    lines.append("| File | Present | Notes |")
    lines.append("|------|---------|-------|")
    for name in [
        "softland_policy.json",
        "enforcer_status.json",
        "enforcer_policy.json",
        "day_rollup.json",
        "device_block.json",
    ]:
        p = BEH / name
        note = ""
        if p.exists():
            try:
                j = json.loads(p.read_text(encoding="utf-8"))
                if name == "day_rollup.json":
                    note = f"productive_min={j.get('productive_minutes')} sessions={j.get('session_count')}"
                elif name == "softland_policy.json":
                    sr = j.get("site_rules") or {}
                    note = (
                        f"enabled={j.get('softland_enabled')} "
                        f"allow={len(sr.get('allow_extra') or [])} "
                        f"watch={len(sr.get('watch_extra') or [])} "
                        f"block={len(sr.get('block_extra') or [])}"
                    )
                elif name == "enforcer_status.json":
                    note = f"armed={j.get('armed')} owns={j.get('owns')} pid={j.get('pid')}"
                elif name == "enforcer_policy.json":
                    note = f"armed={j.get('hard_block_armed')} exes={len(j.get('exes') or [])}"
            except Exception as e:
                note = str(e)
        lines.append(f"| `{name}` | {'yes' if p.exists() else 'NO'} | {note} |")
    lines.append("")

    # Gateway feature matrix
    today = date.today().isoformat()
    week_ago = (date.today() - timedelta(days=7)).isoformat()
    week_ahead = (date.today() + timedelta(days=7)).isoformat()
    ops = [
        ("status.snapshot", {}),
        ("day.status", {}),
        ("day.loop_snapshot", {}),
        ("reward.status", {}),
        ("ledger.snapshot", {"limit": 5}),
        ("plan.list", {"from": week_ago, "to": week_ahead}),
        ("routine.list", {}),
        ("device_block.status", {}),
    ]
    lines.append("## Gateway feature checks (backend brain)")
    lines.append("")
    lines.append("| Feature op | OK | Detail |")
    lines.append("|------------|----|--------|")
    results = []
    for op, payload in ops:
        ok, doc = gateway(op, payload)
        detail = ""
        if isinstance(doc, dict):
            if op == "plan.list":
                blocks = doc.get("blocks") or []
                detail = f"blocks={len(blocks) if isinstance(blocks, list) else '?'}"
            elif op == "day.loop_snapshot":
                loop = doc.get("loop") or {}
                detail = (
                    f"planned={loop.get('planned_minutes')} "
                    f"tracked={loop.get('tracked_productive_minutes')} "
                    f"tasks={loop.get('tasks_total')}"
                )
            elif op == "reward.status":
                detail = f"available={doc.get('reward_available')} ledger={doc.get('earned_ledger_seconds')}"
            elif not ok:
                detail = str(doc.get("error") or doc)[:120]
            else:
                detail = "ok"
        else:
            detail = str(doc)[:120]
        results.append((op, ok, detail))
        lines.append(f"| `{op}` | {'PASS' if ok else 'FAIL'} | {detail} |")
    lines.append("")

    # SQLite SoT
    lines.append("## SQLite SoT (`productivity.db`)")
    lines.append("")
    if DB.exists():
        con = sqlite3.connect(str(DB))
        con.row_factory = sqlite3.Row
        for t in ["planner_blocks", "planner_routines", "tracked_sessions", "productivity_day_tasks"]:
            try:
                n = con.execute(f"select count(*) c from {t}").fetchone()["c"]
                lines.append(f"- `{t}`: **{n}** rows")
            except Exception as e:
                lines.append(f"- `{t}`: error {e}")
        # today blocks
        try:
            rows = con.execute(
                "select id, title, start, status from planner_blocks "
                "where date(start)=? or start like ? limit 20",
                (today, today + "%"),
            ).fetchall()
            lines.append("")
            lines.append(f"### Plan blocks for {today}")
            lines.append("")
            if not rows:
                lines.append("_None — Home/Plan UI will look empty until you add blocks._")
            else:
                lines.append("| id | title | start | status |")
                lines.append("|----|-------|-------|--------|")
                for r in rows:
                    lines.append(f"| {r['id']} | {r['title']} | {r['start']} | {r['status']} |")
        except Exception as e:
            # schema may use start_at
            cols = [x[1] for x in con.execute("pragma table_info(planner_blocks)")]
            lines.append(f"planner_blocks columns: {cols}")
            lines.append(f"query err: {e}")
            sample = con.execute("select * from planner_blocks order by rowid desc limit 3").fetchall()
            for r in sample:
                lines.append(f"- {dict(r)}")
        con.close()
    else:
        lines.append("_DB missing_")
    lines.append("")

    # Root cause / verdict
    gw_ok = all(ok for _, ok, _ in results[:4])
    lines.append("## Verdict")
    lines.append("")
    if not enf:
        lines.append("**FAIL — calt_enforcer not running.** Focus UI cannot populate live plan/day/reward via pipe; Settings writes fail.")
    elif not gw_ok:
        lines.append("**FAIL — gateway unhealthy.** Restart enforcer.")
    elif not foc:
        lines.append("**WARN — enforcer OK but Focus not running.** Start `calt_focus.exe` and Reload UI.")
    else:
        lines.append("**Backend path healthy.** If UI still looks empty, it is mostly **empty SoT / SoftLand OFF / zero tracked minutes**, not a broken FE↔BE wire.")
    lines.append("")
    lines.append("### Why the UI can look empty even when connected")
    lines.append("")
    lines.append("1. SoftLand is **OFF** → mode shows OFF; site lists may be sparse (`watch_extra` empty).")
    lines.append("2. `day_rollup` productive minutes = **0** → GlanceBar / goals ring look empty.")
    lines.append("3. No planner blocks for today → Home/Plan calendar empty.")
    lines.append("4. Arm `exes: []` → Apps list empty (suite restore wiped kill list).")
    lines.append("5. Focus must use tray **Reload UI** after `npm run build:focus` / enforcer restart.")
    lines.append("")
    lines.append("### Owner actions")
    lines.append("")
    lines.append("1. Keep `calt_enforcer` running (or install service).")
    lines.append("2. Tray → **Reload UI**.")
    lines.append("3. Plan tab → add today’s blocks (or import routine).")
    lines.append("4. Settings → SoftLand ON + re-add watch/always lists if needed.")
    lines.append("5. Blocker → re-add game EXEs + Arm if you want kills.")
    lines.append("")

    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    print(f"\nWrote {OUT}")
    return 0 if enf and gw_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
