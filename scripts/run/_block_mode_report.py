"""One-shot SoftLand decide matrix for block report (fixture under TEMP)."""
from __future__ import annotations

import json
import os
import shutil
import struct
import subprocess
import tempfile
from datetime import datetime
from pathlib import Path

def find_repo() -> Path:
    p = Path(__file__).resolve().parent
    for _ in range(8):
        if (p / "AGENTS.md").exists() and (p / "data").exists():
            return p
        p = p.parent
    raise SystemExit("repo root not found")


repo = find_repo()
src = repo / "data/productivity/behavior/softland_policy.json"
base = json.loads(src.read_text(encoding="utf-8"))

tmp = tempfile.mkdtemp(prefix="calt_block_report_")
beh = Path(tmp) / "behavior"
beh.mkdir(parents=True)

exe = str(repo / "calt-focus/backend/calt_msg_host/build/calt_msg_host.exe")


def write_doc(doc: dict) -> None:
    (beh / "softland_policy.json").write_text(json.dumps(doc), encoding="utf-8")


def ask(url: str) -> dict:
    env = os.environ.copy()
    env["CALT_DATA_DIR"] = tmp
    raw = json.dumps({"type": "get_mode", "url": url}).encode()
    p = subprocess.Popen(
        [exe],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    assert p.stdin and p.stdout
    p.stdin.write(struct.pack("<I", len(raw)) + raw)
    p.stdin.close()
    hdr = p.stdout.read(4)
    n = struct.unpack("<I", hdr)[0]
    return json.loads(p.stdout.read(n).decode())


def row(u: str, r: dict) -> str:
    return f"{u:40} {str(r.get('action')):8} {str(r.get('mode')):6} {str(r.get('reason')):28} {r.get('enforce')}"


urls = [
    "https://youtube.com",
    "https://meet.google.com",
    "https://pornhub.com",
    "https://reddit.com",
    "https://wikipedia.org",
    "https://twitter.com",
    "https://github.com",
    "https://facebook.com",
]

hdr = f"{'url':40} {'action':8} {'mode':6} {'reason':28} enforce"

doc = json.loads(json.dumps(base))
doc["softland_enabled"] = True
doc["site_rules"] = {
    "allow_extra": ["meet.google.com", "wikipedia.org"],
    "watch_extra": ["youtube.com", "reddit.com"],
    "block_extra": ["twitter.com", "x.com"],
}
doc["schedules"] = {
    "enabled": True,
    "windows": [
        {
            "id": "w1",
            "label": "AllDayStudy",
            "days": [0, 1, 2, 3, 4, 5, 6],
            "start": "00:00",
            "end": "23:59",
            "mode": "study",
        }
    ],
}
doc["runtime"]["free_until"] = None
doc["runtime"]["incubation_until"] = None
doc["runtime"]["reward_day_active"] = False
write_doc(doc)

print("FIXTURE", tmp)
print("NOW", datetime.now().isoformat(timespec="seconds"))
print("\n=== SOFTLAND ON · schedule study all-day ===")
print(hdr)
for u in urls:
    print(row(u, ask(u)))

doc2 = json.loads(json.dumps(doc))
doc2["runtime"]["free_until"] = "2099-12-31T23:59:59"
doc2["runtime"]["reward_day_active"] = True
write_doc(doc2)
print("\n=== SOFTLAND ON · free_until 2099 (FREE) ===")
print(hdr)
for u in urls:
    print(row(u, ask(u)))

doc3 = json.loads(json.dumps(doc))
doc3["softland_enabled"] = False
write_doc(doc3)
print("\n=== SOFTLAND OFF ===")
print(hdr)
for u in ["https://youtube.com", "https://pornhub.com", "https://twitter.com"]:
    print(row(u, ask(u)))

print("\nDONE")
