"""Refresh day_rollup.json using true local-day UTC bounds (IST-safe)."""
from __future__ import annotations

import json
import sqlite3
from datetime import datetime, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(r"c:\Users\Lenovo\Desktop\Cognitive-Aware Learning Tutor")
DB = ROOT / "data/productivity/productivity.db"
OUT = ROOT / "data/productivity/behavior/day_rollup.json"
TZ = ZoneInfo("Asia/Kolkata")


def main() -> None:
    now = datetime.now(TZ)
    day_start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    day_end = day_start + timedelta(days=1)
    start_z = day_start.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
    end_z = day_end.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"

    c = sqlite3.connect(DB)
    rows = list(
        c.execute(
            """
            SELECT app_name, start_time, end_time, category, source
            FROM tracked_sessions
            WHERE start_time < ? AND (end_time IS NULL OR end_time = '' OR end_time > ?)
              AND IFNULL(source,'') IN ('desktop_tracker','selftracker','extension','native')
            """,
            (end_z, start_z),
        )
    )
    # crude productive: cursor / code editors count as productive for display smoke
    productive_apps = {"cursor.exe", "code.exe", "devenv.exe", "notepad.exe", "calt_focus.exe"}
    prod_sec = 0.0
    for app, start, end, _cat, _src in rows:
        try:
            a = datetime.fromisoformat(start.replace("Z", "+00:00"))
            b = datetime.fromisoformat((end or start).replace("Z", "+00:00"))
        except Exception:
            continue
        sec = max(0.0, (b - a).total_seconds())
        if (app or "").lower() in productive_apps or (_cat or "").lower() in {
            "work",
            "study",
            "coding",
            "productive",
        }:
            prod_sec += sec

    payload = {
        "schema_version": 1,
        "local_date": day_start.date().isoformat(),
        "updated_at": now.replace(tzinfo=None).isoformat(timespec="seconds"),
        "productive_minutes": int(prod_sec // 60),
        "productive_seconds": int(prod_sec),
        "threshold": 60,
        "session_count": len(rows),
        "day_unlocked": False,
        "goal_met": False,
        "source": "manual_refresh_local_day",
        "rules_loaded": True,
        "day_start_utc": start_z,
        "day_end_utc": end_z,
    }
    OUT.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2))
    print("latest5", rows[-5:] if rows else rows)


if __name__ == "__main__":
    main()
