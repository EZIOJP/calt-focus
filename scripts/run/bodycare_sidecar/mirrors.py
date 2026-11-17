"""Write Focus behavior mirrors for body care (latest + day rollup)."""
from __future__ import annotations

import json
import os
import time
from datetime import date, datetime
from pathlib import Path
from typing import Any


def repo_behavior_dir() -> Path:
    # scripts/run/bodycare_sidecar → calt-focus/data/productivity/behavior
    here = Path(__file__).resolve()
    root = here.parents[3]
    return root / "data" / "productivity" / "behavior"


def _atomic_write(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    os.replace(tmp, path)


def empty_day_summary(day: str) -> dict[str, Any]:
    return {
        "date": day,
        "samples": 0,
        "present_sec": 0.0,
        "away_sec": 0.0,
        "attention_sum": 0.0,
        "attention_n": 0,
        "posture_sum": 0.0,
        "posture_n": 0,
        "slouch_sec": 0.0,
        "blink_sum": 0.0,
        "blink_n": 0,
        "distance_sum": 0.0,
        "distance_n": 0,
        "sit_hint_sec": 0.0,
        "stand_hint_sec": 0.0,
        "eye_rest_nudges": 0,
        "updated_at": None,
    }


def apply_sample(summary: dict[str, Any], sample: dict[str, Any], dt_sec: float) -> None:
    dt = max(0.05, min(5.0, float(dt_sec)))
    summary["samples"] = int(summary.get("samples") or 0) + 1
    if sample.get("present"):
        summary["present_sec"] = float(summary.get("present_sec") or 0) + dt
    else:
        summary["away_sec"] = float(summary.get("away_sec") or 0) + dt
    attn = sample.get("attention_pct")
    if isinstance(attn, (int, float)):
        summary["attention_sum"] = float(summary.get("attention_sum") or 0) + float(attn)
        summary["attention_n"] = int(summary.get("attention_n") or 0) + 1
    post = sample.get("posture_score")
    if isinstance(post, (int, float)):
        summary["posture_sum"] = float(summary.get("posture_sum") or 0) + float(post)
        summary["posture_n"] = int(summary.get("posture_n") or 0) + 1
        if float(post) < 45:
            summary["slouch_sec"] = float(summary.get("slouch_sec") or 0) + dt
    blink = sample.get("blink_rate")
    if isinstance(blink, (int, float)):
        summary["blink_sum"] = float(summary.get("blink_sum") or 0) + float(blink)
        summary["blink_n"] = int(summary.get("blink_n") or 0) + 1
    dist = sample.get("distance_proxy")
    if isinstance(dist, (int, float)) and dist > 0:
        summary["distance_sum"] = float(summary.get("distance_sum") or 0) + float(dist)
        summary["distance_n"] = int(summary.get("distance_n") or 0) + 1
    hint = str(sample.get("sit_stand_hint") or "")
    if hint == "sit":
        summary["sit_hint_sec"] = float(summary.get("sit_hint_sec") or 0) + dt
    elif hint == "stand":
        summary["stand_hint_sec"] = float(summary.get("stand_hint_sec") or 0) + dt
    summary["updated_at"] = datetime.now().isoformat(timespec="seconds")


def finalize_day(summary: dict[str, Any]) -> dict[str, Any]:
    out = dict(summary)
    an = int(out.get("attention_n") or 0)
    pn = int(out.get("posture_n") or 0)
    bn = int(out.get("blink_n") or 0)
    dn = int(out.get("distance_n") or 0)
    out["attention_pct_avg"] = round(float(out.get("attention_sum") or 0) / an, 1) if an else None
    out["posture_score_avg"] = round(float(out.get("posture_sum") or 0) / pn, 1) if pn else None
    out["blink_rate_avg"] = round(float(out.get("blink_sum") or 0) / bn, 2) if bn else None
    out["distance_proxy_avg"] = round(float(out.get("distance_sum") or 0) / dn, 3) if dn else None
    out["present_minutes"] = round(float(out.get("present_sec") or 0) / 60.0, 1)
    out["slouch_minutes"] = round(float(out.get("slouch_sec") or 0) / 60.0, 1)
    return out


class MirrorWriter:
    def __init__(self, behavior_dir: Path | None = None) -> None:
        self.behavior = behavior_dir or repo_behavior_dir()
        self.days_dir = self.behavior / "body_care_days"
        self.latest_path = self.behavior / "body_care.json"
        self.day = date.today().isoformat()
        self.summary = self._load_day(self.day)
        self._last_ts = time.time()

    def _day_path(self, day: str) -> Path:
        return self.days_dir / f"{day}.json"

    def _load_day(self, day: str) -> dict[str, Any]:
        path = self._day_path(day)
        if path.is_file():
            try:
                return json.loads(path.read_text(encoding="utf-8"))
            except Exception:
                pass
        return empty_day_summary(day)

    def write_sample(
        self,
        sample: dict[str, Any],
        calibration: dict[str, Any] | None = None,
        control: dict[str, Any] | None = None,
    ) -> None:
        now = time.time()
        dt = now - self._last_ts
        self._last_ts = now
        today = date.today().isoformat()
        if today != self.day:
            self.day = today
            self.summary = self._load_day(today)
        apply_sample(self.summary, sample, dt)
        day_out = finalize_day(self.summary)
        _atomic_write(self._day_path(self.day), day_out)

        payload = {
            "schema": 1,
            "source": "bodycare_sidecar",
            "updated_at": datetime.now().isoformat(timespec="seconds"),
            "sample": sample,
            "day": day_out,
            "calibration": calibration or {},
            "control": control or {},
        }
        _atomic_write(self.latest_path, payload)
