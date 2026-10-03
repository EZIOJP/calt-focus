from scripts.desktop_tracker.run._gw_pipe import gateway
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo
from dateutil import parser

TZ = ZoneInfo("Asia/Kolkata")
now = datetime.now(TZ)
start = now.replace(hour=0, minute=0, second=0, microsecond=0)
end = start + timedelta(days=1)
sess = (
    gateway("plan.overlay", {"from": start.isoformat(), "to": end.isoformat()})
    .get("overlay", {})
    .get("sessions")
    or []
)
items = sorted(
    [
        (
            parser.isoparse(s["start_time"]),
            parser.isoparse(s["end_time"]),
            (s.get("app_name") or "").lower(),
        )
        for s in sess
    ]
)
merged = []
for a, b, app in items:
    if merged and merged[-1][2] == app and (a - merged[-1][1]).total_seconds() <= 900:
        merged[-1] = (merged[-1][0], max(merged[-1][1], b), app)
    else:
        merged.append((a, b, app))
print("raw", len(items), "merged", len(merged))
print(
    "ge120",
    sum(1 for a, b, _ in merged if (b - a).total_seconds() >= 120),
    "ge45",
    sum(1 for a, b, _ in merged if (b - a).total_seconds() >= 45),
)
for a, b, app in merged:
    d = (b - a).total_seconds()
    if d >= 45:
        print(
            f"  {d:6.0f}s {app:28s} {a.astimezone(TZ).strftime('%H:%M')}–{b.astimezone(TZ).strftime('%H:%M')}"
        )
