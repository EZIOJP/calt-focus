#!/usr/bin/env python3
"""
Pull Amazfit/Zepp day health from ZeppBridge's local DB into Focus mirrors.

ZeppBridge Local REST (127.0.0.1:43921) only exposes /health + /workouts/{id}/series.
Daily sleep/steps/HR live in data/zepp.db next to zeppbridge.exe — we read that
read-only and write wearable_*.json / life_*.json plus a rich collector archive.

Schema 3 payload.collector mirrors ZeppBridge AI daily columns (null ≠ 0) while
keeping FE-compatible top-level summary fields.

Config (optional): data/productivity/behavior/zeppbridge.json
  {
    "enabled": true,
    "db_path": "C:/Users/Lenovo/AppData/Local/ZeppBridge/data/zepp.db",
    "api_base": "http://127.0.0.1:43921",
    "api_token": "zbk_…",
    "days": 14
  }

Usage:
  python scripts/run/pull_zeppbridge.py
  python scripts/run/pull_zeppbridge.py --day 2026-11-16
"""
from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
import urllib.error
import urllib.request
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[2]
BEHAVIOR = REPO / "data" / "productivity" / "behavior"
COLLECTOR_DIR = BEHAVIOR / "zeppbridge_collector"
DEFAULT_DB = Path(os.environ.get("LOCALAPPDATA", "")) / "ZeppBridge" / "data" / "zepp.db"
CONFIG_PATH = BEHAVIOR / "zeppbridge.json"
SOURCE = "zeppbridge"
COLLECTOR_SCHEMA = "calt.zeppbridge.collector"
COLLECTOR_SCHEMA_VERSION = 1
PAYLOAD_SCHEMA = 3

# ZeppBridge AI daily-metrics columns (1:1 with daily_metrics.metric when present).
COLLECTOR_DAILY_KEYS: tuple[str, ...] = (
    "resting_hr",
    "readiness",
    "hybrid_charge",
    "physical_charge",
    "mental_charge",
    "stress",
    "stress_min",
    "stress_max",
    "respiratory_rate",
    "respiratory_rate_min",
    "respiratory_rate_max",
    "sleep_hrv",
    "sleep_rhr",
    "hrv_baseline",
    "rhr_baseline",
    "ahi_baseline",
    "spo2_odi",
    "spo2_night_score",
    "spo2_measured_minutes",
    "training_load",
    "pai_daily",
    "pai_total",
    "steps",
    "active_calories",
    "active_minutes",
    "calories",
    "distance",
    "device_max_hr",
    "device_resting_hr",
    "hrv_readiness",
    "rhr_readiness",
)

# ZeppBridge metric_samples names (1:1 with DB). spo2 is night/day SpO₂ series.
SAMPLE_METRICS = ("heart_rate", "hrv", "stress", "hrv_rmssd", "spo2")
SAMPLE_BUCKET_SEC = 10 * 60  # 10-minute buckets
SAMPLE_CAP = 200


def load_config() -> dict[str, Any]:
    cfg: dict[str, Any] = {
        "enabled": True,
        "db_path": str(DEFAULT_DB),
        "api_base": "http://127.0.0.1:43921",
        "api_token": "",
        "days": 14,
    }
    if CONFIG_PATH.is_file():
        try:
            raw = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
            if isinstance(raw, dict):
                cfg.update({k: v for k, v in raw.items() if v is not None})
        except Exception:
            pass
    return cfg


def save_config(cfg: dict[str, Any]) -> None:
    BEHAVIOR.mkdir(parents=True, exist_ok=True)
    # Never print token; keep file local.
    CONFIG_PATH.write_text(json.dumps(cfg, indent=2) + "\n", encoding="utf-8")


def local_tz() -> timezone:
    return datetime.now().astimezone().tzinfo or timezone(timedelta(hours=5, minutes=30))


def parse_iso(s: str) -> datetime:
    s = (s or "").replace("Z", "+00:00")
    return datetime.fromisoformat(s)


def to_local(dt: datetime) -> datetime:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(local_tz())


def clock_min(dt: datetime) -> int:
    return dt.hour * 60 + dt.minute


def open_db(path: Path) -> sqlite3.Connection:
    if not path.is_file():
        raise FileNotFoundError(f"ZeppBridge DB missing: {path}")
    uri = path.resolve().as_uri() + "?mode=ro"
    con = sqlite3.connect(uri, uri=True)
    con.row_factory = sqlite3.Row
    return con


def metrics_for_day(con: sqlite3.Connection, day: str) -> dict[str, float]:
    """All daily_metrics for `day`. Missing keys are absent (not zero)."""
    out: dict[str, float] = {}
    for row in con.execute(
        "SELECT metric, value FROM daily_metrics WHERE date=? ORDER BY id",
        (day,),
    ):
        # Prefer later rows when duplicates (e.g. steps twice).
        try:
            out[str(row["metric"])] = float(row["value"])
        except (TypeError, ValueError):
            continue
    return out


def metric_or_none(m: dict[str, float], key: str) -> float | None:
    if key not in m:
        return None
    try:
        return float(m[key])
    except (TypeError, ValueError):
        return None


def sleep_for_wake_day(con: sqlite3.Connection, day: str) -> dict[str, Any] | None:
    """Pick the main sleep session that ends on local `day` (wake morning).

    Never borrow yesterday's sleep — that fabricated a full hypnogram on a day
    with no wake yet (and empty daily_metrics / samples).
    """
    rows = list(
        con.execute(
            "SELECT * FROM sleep_sessions WHERE date(end_time, 'localtime')=? "
            "OR substr(end_time,1,10)=? "
            "ORDER BY duration_minutes DESC",
            (day, day),
        )
    )
    if not rows:
        return None

    matched: list[sqlite3.Row] = []
    for r in rows:
        end_l = to_local(parse_iso(r["end_time"]))
        if end_l.date().isoformat() == day:
            matched.append(r)
    if not matched:
        # substr match can be UTC date; keep only true local wake-day ends.
        return None
    # Prefer device (band) row over duplicate official cloud copy.
    best = matched[0]
    for r in matched:
        if str(r["sleep_id"] or "").startswith("band:"):
            best = r
            break

    start_l = to_local(parse_iso(best["start_time"]))
    end_l = to_local(parse_iso(best["end_time"]))
    stages = []
    for st in con.execute(
        "SELECT stage, start_time, end_time FROM sleep_stages WHERE sleep_id=? ORDER BY start_time",
        (best["sleep_id"],),
    ):
        s0 = to_local(parse_iso(st["start_time"]))
        s1 = to_local(parse_iso(st["end_time"]))
        day_midnight = datetime.combine(end_l.date(), datetime.min.time(), tzinfo=local_tz())
        start_from_wake_midnight = int((s0 - day_midnight).total_seconds() // 60)
        end_from_wake_midnight = int((s1 - day_midnight).total_seconds() // 60)
        a = start_from_wake_midnight if start_from_wake_midnight >= 0 else start_from_wake_midnight + 1440
        b = end_from_wake_midnight if end_from_wake_midnight >= 0 else end_from_wake_midnight + 1440
        label = str(st["stage"] or "asleep")
        if label not in ("deep", "rem", "light", "awake"):
            label = "asleep"
        stages.append({"label": label, "start": a, "stop": b})

    start_min_focus = clock_min(start_l)
    end_min_focus = clock_min(end_l)

    return {
        "score": int(best["score"] or 0),
        "total_min": int(best["duration_minutes"] or 0),
        "deep_min": int(best["deep_minutes"] or 0),
        "light_min": int(best["light_minutes"] or 0),
        "rem_min": int(best["rem_minutes"] or 0),
        "awake_min": int(best["awake_minutes"] or 0),
        "wake_count": int(best["wake_count"]) if best["wake_count"] is not None else None,
        "start_min": start_min_focus,
        "end_min": end_min_focus,
        "start_at": start_l.replace(tzinfo=None).isoformat(timespec="seconds"),
        "end_at": end_l.replace(tzinfo=None).isoformat(timespec="seconds"),
        "stages": stages,
        "sleep_id": best["sleep_id"],
        "provider": best["provider"],
        "source_scope": best["source_scope"],
    }


def workouts_for_day(con: sqlite3.Connection, day: str) -> list[dict[str, Any]]:
    out = []
    for r in con.execute(
        "SELECT * FROM workouts WHERE substr(start_time,1,10)=? OR "
        "date(start_time,'localtime')=? ORDER BY start_time",
        (day, day),
    ):
        start_l = to_local(parse_iso(r["start_time"]))
        end_l = to_local(parse_iso(r["end_time"])) if r["end_time"] else None
        if start_l.date().isoformat() != day and (not end_l or end_l.date().isoformat() != day):
            if str(r["start_time"])[:10] != day:
                continue
        dur = None
        if end_l:
            dur = max(0, int((end_l - start_l).total_seconds() // 60))
        out.append(
            {
                "type": r["workout_type"],
                "workout_id": r["workout_id"],
                "start": start_l.isoformat(),
                "end": end_l.isoformat() if end_l else None,
                "duration_min": dur,
                "calories": r["calories"],
                "distance_m": r["distance_meters"],
                "avg_hr": r["avg_hr"],
                "max_hr": r["max_hr"],
            }
        )
    return out


def samples_for_range(
    con: sqlite3.Connection,
    start_local: datetime,
    end_local: datetime,
    *,
    bucket_sec: int = SAMPLE_BUCKET_SEC,
    cap: int = SAMPLE_CAP,
) -> dict[str, list[dict[str, Any]]]:
    """Downsample metric_samples in [start_local, end_local). Empty list ≠ zero."""
    out: dict[str, list[dict[str, Any]]] = {k: [] for k in SAMPLE_METRICS}
    if end_local <= start_local:
        return out
    start_utc = start_local.astimezone(timezone.utc).isoformat()
    end_utc = end_local.astimezone(timezone.utc).isoformat()
    try:
        placeholders = ",".join("?" for _ in SAMPLE_METRICS)
        rows = list(
            con.execute(
                "SELECT metric, timestamp, value FROM metric_samples "
                f"WHERE timestamp >= ? AND timestamp < ? AND metric IN ({placeholders}) "
                "ORDER BY timestamp",
                (start_utc, end_utc, *SAMPLE_METRICS),
            )
        )
    except sqlite3.Error:
        return out

    buckets: dict[str, dict[int, list[float]]] = {k: {} for k in SAMPLE_METRICS}
    for r in rows:
        metric = str(r["metric"] or "")
        if metric not in buckets:
            continue
        try:
            ts = to_local(parse_iso(str(r["timestamp"])))
            val = float(r["value"])
        except (TypeError, ValueError):
            continue
        bucket = int(ts.timestamp()) // bucket_sec
        buckets[metric].setdefault(bucket, []).append(val)

    for metric, by_b in buckets.items():
        pts: list[dict[str, Any]] = []
        for b in sorted(by_b):
            vals = by_b[b]
            if not vals:
                continue
            mid = datetime.fromtimestamp(b * bucket_sec + bucket_sec // 2, tz=local_tz())
            pts.append(
                {
                    "t": mid.replace(tzinfo=None).isoformat(timespec="seconds"),
                    "v": round(sum(vals) / len(vals), 2),
                    "n": len(vals),
                }
            )
        if len(pts) > cap:
            step = max(1, len(pts) // cap)
            pts = pts[::step][:cap]
        out[metric] = pts
    return out


def merge_sample_maps(
    primary: dict[str, list[dict[str, Any]]],
    extra: dict[str, list[dict[str, Any]]],
) -> dict[str, list[dict[str, Any]]]:
    """Union by timestamp; prefer denser primary when both have points."""
    out: dict[str, list[dict[str, Any]]] = {k: [] for k in SAMPLE_METRICS}
    for metric in SAMPLE_METRICS:
        by_t: dict[str, dict[str, Any]] = {}
        for pt in extra.get(metric) or []:
            t = str(pt.get("t") or "")
            if t:
                by_t[t] = pt
        for pt in primary.get(metric) or []:
            t = str(pt.get("t") or "")
            if t:
                by_t[t] = pt
        pts = [by_t[k] for k in sorted(by_t)]
        if len(pts) > SAMPLE_CAP:
            step = max(1, len(pts) // SAMPLE_CAP)
            pts = pts[::step][:SAMPLE_CAP]
        out[metric] = pts
    return out


def samples_for_day(
    con: sqlite3.Connection,
    day: str,
    *,
    bucket_sec: int = SAMPLE_BUCKET_SEC,
    cap: int = SAMPLE_CAP,
) -> dict[str, list[dict[str, Any]]]:
    """Downsample metric_samples for the local calendar day (null-safe lists)."""
    day0 = datetime.combine(date.fromisoformat(day), datetime.min.time(), tzinfo=local_tz())
    day1 = day0 + timedelta(days=1)
    return samples_for_range(con, day0, day1, bucket_sec=bucket_sec, cap=cap)


def api_health(base: str, token: str) -> dict[str, Any] | None:
    if not base:
        return None
    req = urllib.request.Request(
        base.rstrip("/") + "/health",
        headers={"Authorization": f"Bearer {token}"} if token else {},
        method="GET",
    )
    try:
        with urllib.request.urlopen(req, timeout=3) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, json.JSONDecodeError):
        return None


def build_collector_daily(m: dict[str, float]) -> dict[str, float | None]:
    daily: dict[str, float | None] = {k: metric_or_none(m, k) for k in COLLECTOR_DAILY_KEYS}
    # Prefer active_calories for AI-shaped column when only calories exists.
    if daily.get("active_calories") is None and metric_or_none(m, "calories") is not None:
        daily["active_calories"] = metric_or_none(m, "calories")
    return daily


def build_coverage(
    sleep: dict[str, Any] | None,
    m: dict[str, float],
    samples: dict[str, list[dict[str, Any]]],
    workouts: list[dict[str, Any]],
) -> dict[str, bool]:
    return {
        "sleep": bool(sleep and float(sleep.get("total_min") or 0) > 0),
        "recovery": metric_or_none(m, "readiness") is not None
        or metric_or_none(m, "hybrid_charge") is not None
        or metric_or_none(m, "physical_charge") is not None,
        "resting_hr": metric_or_none(m, "resting_hr") is not None
        or metric_or_none(m, "sleep_rhr") is not None
        or metric_or_none(m, "device_resting_hr") is not None,
        "heart_rate": bool(samples.get("heart_rate")),
        "training": metric_or_none(m, "training_load") is not None or bool(workouts),
    }


def build_day(con: sqlite3.Connection, day: str, synced_at: str) -> dict[str, Any]:
    m = metrics_for_day(con, day)
    sleep = sleep_for_wake_day(con, day)
    workouts = workouts_for_day(con, day)
    samples = samples_for_day(con, day)
    # Overnight sleep often starts previous calendar day — fold that window's
    # HR/HRV/stress/SpO₂ into wake-day samples (Zepp app does the same).
    if sleep and sleep.get("start_at") and sleep.get("end_at"):
        try:
            s0 = parse_iso(str(sleep["start_at"]))
            s1 = parse_iso(str(sleep["end_at"]))
            if s0.tzinfo is None:
                s0 = s0.replace(tzinfo=local_tz())
            if s1.tzinfo is None:
                s1 = s1.replace(tzinfo=local_tz())
            sleep_samples = samples_for_range(con, s0, s1 + timedelta(minutes=1))
            samples = merge_sample_maps(samples, sleep_samples)
        except (TypeError, ValueError):
            pass

    steps_n = metric_or_none(m, "steps")
    step_goal_n = metric_or_none(m, "step_goal")
    calories_n = metric_or_none(m, "active_calories")
    if calories_n is None:
        calories_n = metric_or_none(m, "calories")
    calorie_goal_n = metric_or_none(m, "calorie_goal")
    distance_n = metric_or_none(m, "distance")
    stress_n = metric_or_none(m, "stress")
    stress_min_n = metric_or_none(m, "stress_min")
    stress_max_n = metric_or_none(m, "stress_max")
    hr_rest_n = (
        metric_or_none(m, "resting_hr")
        or metric_or_none(m, "sleep_rhr")
        or metric_or_none(m, "device_resting_hr")
    )
    hr_max_n = metric_or_none(m, "device_max_hr")
    pai_n = metric_or_none(m, "pai_daily")
    if pai_n is None:
        pai_n = metric_or_none(m, "pai_total")
    spo2_score_n = metric_or_none(m, "spo2_night_score")
    readiness_n = metric_or_none(m, "readiness")
    training_load_n = metric_or_none(m, "training_load")
    hybrid_n = metric_or_none(m, "hybrid_charge")
    physical_n = metric_or_none(m, "physical_charge")
    mental_n = metric_or_none(m, "mental_charge")
    sleep_hrv_n = metric_or_none(m, "sleep_hrv")
    active_minutes_n = metric_or_none(m, "active_minutes")

    # Null-safe: missing Zepp cells stay null (never fabricate 0).
    steps = int(steps_n) if steps_n is not None else None
    step_goal = int(step_goal_n) if step_goal_n is not None else (8000 if steps is not None else None)
    calories = int(calories_n) if calories_n is not None else None
    calorie_goal = (
        int(calorie_goal_n) if calorie_goal_n is not None else (300 if calories is not None else None)
    )
    distance = float(distance_n) if distance_n is not None else None
    stress = int(stress_n) if stress_n is not None else None
    hr_rest = int(hr_rest_n) if hr_rest_n is not None else None
    hr_max = int(hr_max_n) if hr_max_n is not None else None
    pai = float(pai_n) if pai_n is not None else None
    spo2_ui = (
        float(spo2_score_n)
        if spo2_score_n is not None and 50 <= float(spo2_score_n) <= 100
        else None
    )

    total_min = float(sleep["total_min"]) if sleep else 0.0
    hours = round(total_min / 60.0, 2) if total_min else None

    sleep_obj = None
    if sleep:
        sleep_obj = {
            "score": sleep["score"],
            "total_min": sleep["total_min"],
            "deep_min": sleep["deep_min"],
            "light_min": sleep.get("light_min"),
            "rem_min": sleep.get("rem_min"),
            "awake_min": sleep.get("awake_min"),
            "wake_count": sleep.get("wake_count"),
            "start_min": sleep["start_min"],
            "end_min": sleep["end_min"],
            "start_at": sleep["start_at"],
            "end_at": sleep["end_at"],
            "stages": sleep.get("stages") or [],
            "sleep_id": sleep.get("sleep_id"),
            "provider": sleep.get("provider"),
            "source_scope": sleep.get("source_scope"),
        }

    metrics_all = {k: m[k] for k in sorted(m)}
    collector_daily = build_collector_daily(m)
    coverage = build_coverage(sleep, m, samples, workouts)

    collector = {
        "schema": COLLECTOR_SCHEMA,
        "schema_version": COLLECTOR_SCHEMA_VERSION,
        "local_date": day,
        "synced_at": synced_at,
        "source": SOURCE,
        "coverage": coverage,
        "daily": collector_daily,
        "metrics_all": metrics_all,
        "sleep": sleep_obj,
        "workouts": workouts,
        "samples": {
            "heart_rate": samples.get("heart_rate") or [],
            "hrv": samples.get("hrv") or [],
            "stress": samples.get("stress") or [],
            "hrv_rmssd": samples.get("hrv_rmssd") or [],
            "spo2": samples.get("spo2") or [],
            "bucket_sec": SAMPLE_BUCKET_SEC,
            "note": "Downsampled metric_samples for the local day; empty list means no samples (not zero).",
        },
    }

    payload: dict[str, Any] = {
        "schema": PAYLOAD_SCHEMA,
        "source": SOURCE,
        "dump": "zeppbridge_db",
        "local_date": day,
        "sleep": sleep_obj,
        "workouts": workouts,
        "meta": {
            "app": "zeppbridge",
            "ver": "pull_zeppbridge.py",
            "collector": COLLECTOR_SCHEMA,
            "collector_version": COLLECTOR_SCHEMA_VERSION,
        },
        # Full day metrics (null-safe via absence). Prefer collector.daily for AI shape.
        "metrics_all": metrics_all,
        "metrics_raw": metrics_all,
        "collector": collector,
    }
    # Only emit blocks when Zepp has real values — never fabricate zeroes.
    if steps is not None or step_goal is not None:
        payload["activity"] = {"steps": steps, "target": step_goal}
    if calories is not None or calorie_goal is not None:
        payload["calorie"] = {"kcal": calories, "target": calorie_goal}
    if distance is not None:
        payload["distance"] = {"meters": distance}
    if hr_rest is not None or hr_max is not None:
        payload["heart"] = {
            "last": hr_rest,
            "resting": hr_rest,
            "avg": hr_rest,
            "max": hr_max,
        }
    if stress is not None or stress_min_n is not None or stress_max_n is not None:
        payload["stress"] = {
            "value": stress,
            "min": int(stress_min_n) if stress_min_n is not None else None,
            "max": int(stress_max_n) if stress_max_n is not None else None,
        }
    pai_total_n = metric_or_none(m, "pai_total")
    if pai is not None or pai_total_n is not None:
        payload["pai"] = {"today": pai, "total": pai_total_n}
    if spo2_ui is not None:
        payload["spo2"] = {"value": spo2_ui}

    quality = {
        "sleep_ok": bool(sleep and (hours or 0) > 0),
        "sleep_timed": bool(sleep and sleep.get("start_at") and sleep.get("end_at")),
        "sleep_stages": len(sleep.get("stages") or []) if sleep else 0,
        "hr_series": len(samples.get("heart_rate") or []),
        "hr_ok": hr_rest_n is not None,
        "steps_ok": steps_n is not None,
        "collector_ok": True,
        "readiness_ok": readiness_n is not None,
        "training_ok": training_load_n is not None or bool(workouts),
    }

    summary: dict[str, Any] = {
        "sleep": sleep_obj,
        "sleep_hours": hours,
        "sleep_score": sleep["score"] if sleep else None,
        "sleep_deep_min": sleep["deep_min"] if sleep else None,
        "sleep_start_min": sleep["start_min"] if sleep else None,
        "sleep_end_min": sleep["end_min"] if sleep else None,
        "sleep_start_at": sleep["start_at"] if sleep else None,
        "sleep_end_at": sleep["end_at"] if sleep else None,
        "steps": steps,
        "step_target": step_goal,
        "hr_last": hr_rest,
        "hr_resting": hr_rest,
        "hr_avg": hr_rest,
        "hr_max": hr_max,
        "stress": stress,
        "spo2": spo2_ui,
        "calories": calories,
        "calorie_target": calorie_goal,
        "distance_m": distance,
        "stand_hours": None,
        "battery_pct": None,
        "pai_today": pai,
        # Rich promotions (null when ZeppBridge has no row — FE must treat null as missing).
        "readiness": readiness_n,
        "training_load": training_load_n,
        "hybrid_charge": hybrid_n,
        "physical_charge": physical_n,
        "mental_charge": mental_n,
        "sleep_hrv": sleep_hrv_n,
        "stress_min": stress_min_n,
        "stress_max": stress_max_n,
        "active_minutes": active_minutes_n,
        "pai_total": pai_total_n,
        "data_quality": quality,
        "source": SOURCE,
        "local_date": day,
        "synced_at": synced_at,
        "payload": payload,
    }
    return summary


def life_from_summary(summary: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": 2,
        "local_date": summary.get("local_date"),
        "source": SOURCE,
        "synced_at": summary.get("synced_at"),
        "sleep_hours": summary.get("sleep_hours"),
        "sleep_score": summary.get("sleep_score"),
        "steps": summary.get("steps"),
        "hr_last": summary.get("hr_last"),
        "hr_resting": summary.get("hr_resting"),
        "hr_avg": summary.get("hr_avg"),
        "hr_max": summary.get("hr_max"),
        "stress": summary.get("stress"),
        "spo2": summary.get("spo2"),
        "calories": summary.get("calories"),
        "distance_m": summary.get("distance_m"),
        "readiness": summary.get("readiness"),
        "training_load": summary.get("training_load"),
        "hybrid_charge": summary.get("hybrid_charge"),
        "physical_charge": summary.get("physical_charge"),
        "mental_charge": summary.get("mental_charge"),
        "sleep_hrv": summary.get("sleep_hrv"),
        "stress_min": summary.get("stress_min"),
        "stress_max": summary.get("stress_max"),
        "active_minutes": summary.get("active_minutes"),
        "pai_today": summary.get("pai_today"),
        "pai_total": summary.get("pai_total"),
    }


def sleep_window_from_summary(summary: dict[str, Any]) -> dict[str, Any] | None:
    day = summary.get("local_date")
    start = summary.get("sleep_start_at")
    end = summary.get("sleep_end_at")
    mins = int(round(float(summary.get("sleep_hours") or 0) * 60))
    if not day or not start or not end or mins <= 0:
        return None
    return {"date": day, "start": start, "end": end, "minutes": mins}


def write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def write_collector_archive(summary: dict[str, Any]) -> Path:
    day = str(summary.get("local_date") or "")
    payload = summary.get("payload") if isinstance(summary.get("payload"), dict) else {}
    collector = payload.get("collector") if isinstance(payload, dict) else None
    if not isinstance(collector, dict):
        collector = {
            "schema": COLLECTOR_SCHEMA,
            "schema_version": COLLECTOR_SCHEMA_VERSION,
            "local_date": day,
            "source": SOURCE,
            "error": "missing_collector_block",
        }
    path = COLLECTOR_DIR / f"{day}.json"
    write_json(path, collector)
    return path


def main() -> int:
    ap = argparse.ArgumentParser(description="Pull ZeppBridge DB → Focus wearable mirrors + collector")
    ap.add_argument("--day", help="YYYY-MM-DD (default: today local)")
    ap.add_argument("--days", type=int, help="How many trailing days to write (default from config)")
    ap.add_argument("--db", help="Override path to zepp.db")
    ap.add_argument("--write-config", action="store_true", help="Write/update zeppbridge.json from flags/env")
    ap.add_argument("--token", help="Local REST token (zbk_…); stored in config, not printed")
    args = ap.parse_args()

    cfg = load_config()
    if args.db:
        cfg["db_path"] = args.db
    if args.token:
        cfg["api_token"] = args.token
    if args.days:
        cfg["days"] = args.days
    if args.write_config or args.token or args.db:
        save_config(cfg)
        print(f"Wrote {CONFIG_PATH}")

    db_path = Path(cfg["db_path"])
    today = date.today().isoformat()
    end_day = args.day or today
    n_days = int(args.days or cfg.get("days") or 14)
    start = date.fromisoformat(end_day) - timedelta(days=max(0, n_days - 1))

    health = api_health(str(cfg.get("api_base") or ""), str(cfg.get("api_token") or ""))
    api_ok = bool(health and health.get("status") == "ok")
    print(f"ZeppBridge API: {'ok ' + str(health.get('version','')) if api_ok else 'down/skip'} @ {cfg.get('api_base')}")

    con = open_db(db_path)
    synced_at = datetime.now().astimezone().replace(microsecond=0).isoformat()
    days_dir = BEHAVIOR / "wearable_days"
    days_dir.mkdir(parents=True, exist_ok=True)
    COLLECTOR_DIR.mkdir(parents=True, exist_ok=True)

    windows: list[dict[str, Any]] = []
    wrote = 0
    d = start
    last_summary = None
    while d <= date.fromisoformat(end_day):
        day = d.isoformat()
        summary = build_day(con, day, synced_at)
        write_json(days_dir / f"{day}.json", summary)
        write_collector_archive(summary)
        life = life_from_summary(summary)
        write_json(BEHAVIOR / "life_days" / f"{day}.json", life)
        win = sleep_window_from_summary(summary)
        if win:
            windows.append(win)
        last_summary = summary
        wrote += 1
        rdy = summary.get("readiness")
        tload = summary.get("training_load")
        sh = summary.get("sleep_hours")
        sh_s = f"{sh}h" if sh is not None else "—"
        print(
            f"  {day}: steps={summary['steps']} sleep={sh_s} "
            f"stress={summary['stress']} readiness={rdy} training_load={tload} "
            f"schema={PAYLOAD_SCHEMA} source={SOURCE}"
        )
        d += timedelta(days=1)

    if last_summary and last_summary.get("local_date") == today:
        write_json(BEHAVIOR / "wearable_day.json", last_summary)
        write_json(BEHAVIOR / "life_today.json", life_from_summary(last_summary))
    elif last_summary:
        today_path = days_dir / f"{today}.json"
        if today_path.is_file():
            today_sum = json.loads(today_path.read_text(encoding="utf-8"))
            write_json(BEHAVIOR / "wearable_day.json", today_sum)
            write_json(BEHAVIOR / "life_today.json", life_from_summary(today_sum))

    sw_path = BEHAVIOR / "sleep_windows.json"
    existing: list[dict[str, Any]] = []
    if sw_path.is_file():
        try:
            prev = json.loads(sw_path.read_text(encoding="utf-8"))
            existing = list(prev.get("windows") or [])
        except Exception:
            existing = []
    by_date = {w.get("date"): w for w in existing if isinstance(w, dict) and w.get("date")}
    for w in windows:
        by_date[w["date"]] = w
    merged = sorted(by_date.values(), key=lambda x: str(x.get("date") or ""))
    write_json(
        sw_path,
        {
            "schema_version": 1,
            "updated_at": synced_at,
            "source": SOURCE,
            "windows": merged[-60:],
        },
    )

    ping = {
        "updated_at": synced_at,
        "last_ingest_at": synced_at,
        "last_event": "zeppbridge_pull",
        "last_source": SOURCE,
        "payload_schema": PAYLOAD_SCHEMA,
        "collector_schema": COLLECTOR_SCHEMA,
        "collector_dir": str(COLLECTOR_DIR),
        "api_ok": api_ok,
        "api_version": (health or {}).get("version"),
        "db_path": str(db_path),
        "days_written": wrote,
        "id": f"zb-{synced_at}",
    }
    write_json(BEHAVIOR / "wearables_last_sync.json", ping)
    write_json(BEHAVIOR / "wearables_ping.json", ping)

    print(f"OK: wrote {wrote} day(s) → {BEHAVIOR} (+ {COLLECTOR_DIR.name}/)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr)
        raise SystemExit(1)
