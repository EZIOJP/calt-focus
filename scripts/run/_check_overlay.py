import sqlite3
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

c = sqlite3.connect(r"data/productivity/productivity.db")
TZ = ZoneInfo("Asia/Kolkata")
start = datetime(2026, 9, 14, tzinfo=TZ).astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S") + "Z"
end = datetime(2026, 9, 15, tzinfo=TZ).astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S") + "Z"
print("bounds", start, end)
print(
    "overlap",
    c.execute(
        "select count(*) from tracked_sessions where start_time < ? and (end_time is null or end_time > ?)",
        (end, start),
    ).fetchone(),
)
print(
    "sample",
    list(
        c.execute(
            "select app_name,start_time,end_time,source,user_id from tracked_sessions "
            "where start_time >= '2026-09-13T18:30:00Z' order by start_time desc limit 8"
        )
    ),
)
print(
    "by_source",
    list(
        c.execute(
            "select source, count(1) from tracked_sessions "
            "where start_time >= '2026-09-13T18:30:00Z' group by source"
        )
    ),
)
print(
    "by_user",
    list(
        c.execute(
            "select user_id, count(1) from tracked_sessions "
            "where start_time >= '2026-09-13T18:30:00Z' group by user_id"
        )
    ),
)
