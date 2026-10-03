"""One-shot: create data/productivity/{productivity.db,behavior,bible} from Study tree.

Copies productivity tables out of data/vocab_app.db into data/productivity/productivity.db.
Copies data/behavior → data/productivity/behavior and data/bible → data/productivity/bible
(if targets missing or empty).

Safe to re-run: skips file copy when destination already has content; DB tables
CREATE IF NOT EXISTS then INSERT OR IGNORE / replace from source.
"""
from __future__ import annotations

import shutil
import sqlite3
import sys
from pathlib import Path

# …/scripts/desktop_tracker/build/this.py → repo root is parents[3]
ROOT = Path(__file__).resolve().parents[3]
SRC_DB = ROOT / "data" / "vocab_app.db"
PROD_DIR = ROOT / "data" / "productivity"
DST_DB = PROD_DIR / "productivity.db"
SRC_BEHAVIOR = ROOT / "data" / "behavior"
DST_BEHAVIOR = PROD_DIR / "behavior"
SRC_BIBLE = ROOT / "data" / "bible"
DST_BIBLE = PROD_DIR / "bible"

# Tables owned by calt_enforcer / Focus (not Study content)
TABLES = [
    "productivity_softland",
    "productivity_ledger",
    "productivity_pending_changes",
    "productivity_gateway_meta",
    "productivity_day_passes",
    "productivity_reward_credits",
    "productivity_reward_meta",
    "productivity_day_events",
    "productivity_planner_meta",
    "productivity_journal",
    "productivity_bible_day",
    "planner_blocks",
    "planner_routines",
    "tracked_sessions",
    "enforcer_runtime",
]


def copy_tree_if_needed(src: Path, dst: Path) -> None:
    if not src.is_dir():
        print(f"skip missing {src}")
        return
    if dst.is_dir() and any(dst.iterdir()):
        print(f"keep existing {dst}")
        return
    print(f"copy {src} -> {dst}")
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def migrate_tables() -> None:
    PROD_DIR.mkdir(parents=True, exist_ok=True)
    if not SRC_DB.is_file():
        print(f"no source db {SRC_DB}; creating empty {DST_DB}")
        DST_DB.touch()
        return

    src = sqlite3.connect(str(SRC_DB))
    dst = sqlite3.connect(str(DST_DB))
    src_tables = {
        r[0]
        for r in src.execute(
            "SELECT name FROM sqlite_master WHERE type='table'"
        ).fetchall()
    }
    for name in TABLES:
        if name not in src_tables:
            print(f"skip missing table {name}")
            continue
        # Copy schema
        row = src.execute(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name=?",
            (name,),
        ).fetchone()
        if not row or not row[0]:
            continue
        dst.execute(f"DROP TABLE IF EXISTS {name}")
        dst.execute(row[0])
        cols = [r[1] for r in src.execute(f"PRAGMA table_info({name})").fetchall()]
        col_list = ", ".join(cols)
        placeholders = ", ".join("?" for _ in cols)
        rows = src.execute(f"SELECT {col_list} FROM {name}").fetchall()
        if rows:
            dst.executemany(
                f"INSERT INTO {name} ({col_list}) VALUES ({placeholders})",
                rows,
            )
        print(f"migrated {name}: {len(rows)} rows")
    # indexes (best-effort)
    for r in src.execute(
        "SELECT sql FROM sqlite_master WHERE type='index' AND sql IS NOT NULL"
    ).fetchall():
        sql = r[0]
        if not sql:
            continue
        if any(t in sql for t in TABLES):
            try:
                dst.execute(sql)
            except sqlite3.Error:
                pass
    dst.commit()
    src.close()
    dst.close()
    print(f"wrote {DST_DB}")


def main() -> int:
    migrate_tables()
    copy_tree_if_needed(SRC_BEHAVIOR, DST_BEHAVIOR)
    copy_tree_if_needed(SRC_BIBLE, DST_BIBLE)
    # Marker so Study tools know old paths are legacy
    marker = ROOT / "data" / "behavior" / "MOVED_TO_PRODUCTIVITY.txt"
    try:
        marker.write_text(
            "Mirrors live under data/productivity/behavior/ — owned by calt_enforcer / calt_focus.\n",
            encoding="utf-8",
        )
    except OSError:
        pass
    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
