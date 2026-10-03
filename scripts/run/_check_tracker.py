import sqlite3
from pathlib import Path

root = Path(r"c:\Users\Lenovo\Desktop\Cognitive-Aware Learning Tutor")
db = root / "data/productivity/productivity.db"
c = sqlite3.connect(db)
tables = [r[0] for r in c.execute("select name from sqlite_master where type='table' order by 1")]
print("tables_count", len(tables))
print("has_users", "users" in tables)
print("has_enforcer_runtime", "enforcer_runtime" in tables)
if "enforcer_runtime" in tables:
    print("enforcer_runtime", list(c.execute("select * from enforcer_runtime")))
print(
    "latest",
    list(
        c.execute(
            "select app_name,start_time from tracked_sessions order by start_time desc limit 5"
        )
    ),
)
print(
    "todayish",
    list(
        c.execute(
            "select count(*), max(start_time) from tracked_sessions where start_time >= '2026-09-14'"
        )
    ),
)
log = root / "data/productivity/behavior/tracker_agent.log"
print("agent_log", log.read_text(encoding="utf-8", errors="replace") if log.exists() else "MISSING")
