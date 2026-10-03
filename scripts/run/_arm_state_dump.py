"""Dump enforcer arm/kill-related state from mirrors + sqlite."""
from __future__ import annotations

import json
import sqlite3
from pathlib import Path

def find_repo() -> Path:
    p = Path(__file__).resolve().parent
    for _ in range(8):
        if (p / "AGENTS.md").exists() and (p / "data").exists():
            return p
        p = p.parent
    raise SystemExit("repo root not found")


repo = find_repo()
beh = repo / "data/productivity/behavior"
db = repo / "data/productivity/productivity.db"

print("=== enforcer_status.json ===")
print((beh / "enforcer_status.json").read_text(encoding="utf-8"))
print("=== enforcer_policy.json ===")
print((beh / "enforcer_policy.json").read_text(encoding="utf-8"))

pol = json.loads((beh / "softland_policy.json").read_text(encoding="utf-8"))
print("=== softland master ===")
print(
    json.dumps(
        {
            "softland_enabled": pol.get("softland_enabled"),
            "site_rules": pol.get("site_rules"),
            "schedules": pol.get("schedules"),
            "runtime_keys": list((pol.get("runtime") or {}).keys()),
            "free_until": (pol.get("runtime") or {}).get("free_until"),
            "updated_at": pol.get("updated_at"),
        },
        indent=2,
    )
)

if db.exists():
    con = sqlite3.connect(str(db))
    cur = con.cursor()
    tables = [r[0] for r in cur.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY 1")]
    print("=== sqlite tables ===")
    print(tables)
    for t in tables:
        low = t.lower()
        if any(k in low for k in ("policy", "enforc", "arm", "kill", "soft", "exe", "block")):
            cols = [d[0] for d in cur.execute(f"PRAGMA table_info({t})")]
            rows = cur.execute(f"SELECT * FROM {t} LIMIT 8").fetchall()
            print(f"--- {t} {cols}")
            for r in rows:
                print(r)
else:
    print("no productivity.db")
