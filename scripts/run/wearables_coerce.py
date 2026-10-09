"""Normalize Health Connect / Google Fit / bridge exports → CALT dump shape.

Focus copy of Study ``backend/wearables/health_connect_map.py`` for the
standalone :8765 hub. Missing sensors stay absent — never fabricate zeros.
"""

from __future__ import annotations

from datetime import date, datetime, timezone
from typing import Any


# Top-level keys we treat as first-class dump categories (raw kept in payload_json).
KNOWN_CATEGORIES: tuple[str, ...] = (
    "sleep",
    "heart",
    "activity",
    "calorie",
    "distance",
    "spo2",
    "stress",
    "pai",
    "stand",
    "sitting",
    "battery",
    "fat_burn",
    "temperature",
    "weather",
    "hrv",
    "respiratory",
    "workouts",
    "active_minutes",
    "recovery",
    "vo2max",
)


def _as_int(v: Any) -> int | None:
    if v is None or v == "":
        return None
    try:
        return int(round(float(v)))
    except (TypeError, ValueError):
        return None


def _as_float(v: Any) -> float | None:
    if v is None or v == "":
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def _iso_day(raw: Any, fallback: str | None = None) -> str | None:
    if raw is None:
        return fallback
    if isinstance(raw, date) and not isinstance(raw, datetime):
        return raw.isoformat()
    text = str(raw).strip()
    if not text:
        return fallback
    if len(text) >= 10 and text[4] == "-" and text[7] == "-":
        return text[:10]
    try:
        dt = datetime.fromisoformat(text.replace("Z", "+00:00"))
        return dt.date().isoformat()
    except ValueError:
        return fallback


def inventory_categories(payload: dict[str, Any] | None) -> dict[str, Any]:
    """Report which raw categories are present (not fabricated)."""
    body = payload if isinstance(payload, dict) else {}
    present: list[str] = []
    details: dict[str, Any] = {}
    for key in KNOWN_CATEGORIES:
        val = body.get(key)
        if val is None:
            continue
        if isinstance(val, dict) and not val:
            continue
        if isinstance(val, list) and len(val) == 0:
            continue
        present.append(key)
        if isinstance(val, dict):
            details[key] = {
                "keys": sorted(str(k) for k in val.keys())[:24],
                "series_len": len(val["series"])
                if isinstance(val.get("series"), list)
                else None,
            }
        elif isinstance(val, list):
            details[key] = {"count": len(val)}
        else:
            details[key] = {"value_type": type(val).__name__}
    caps = body.get("capabilities") if isinstance(body.get("capabilities"), dict) else {}
    return {
        "present": present,
        "count": len(present),
        "details": details,
        "capabilities": caps or None,
        "source": body.get("source"),
        "dump": body.get("dump"),
    }


def _merge_sleep_from_hc(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    total = 0
    stages: list[dict[str, Any]] = []
    for rec in records:
        mins = _as_int(rec.get("duration_min") or rec.get("minutes"))
        start = rec.get("start") or rec.get("start_time")
        end = rec.get("end") or rec.get("end_time")
        if mins is None and start and end:
            try:
                a = datetime.fromisoformat(str(start).replace("Z", "+00:00"))
                b = datetime.fromisoformat(str(end).replace("Z", "+00:00"))
                mins = max(0, int((b - a).total_seconds() // 60))
            except ValueError:
                mins = None
        if mins:
            total += mins
        stage = rec.get("stage") or rec.get("sleep_stage")
        if stage and mins:
            stages.append({"stage": str(stage), "minutes": mins})
    if total <= 0 and not stages:
        return None
    out: dict[str, Any] = {"total_min": total or None}
    if stages:
        out["stages"] = stages
    score = None
    for rec in records:
        score = _as_int(rec.get("score") or rec.get("sleep_score"))
        if score is not None:
            break
    if score is not None:
        out["score"] = score
    return out


def normalize_health_connect_records(
    records: list[dict[str, Any]] | dict[str, Any],
    *,
    local_date: str | None = None,
) -> dict[str, Any]:
    """Map a list (or typed dict) of HC-like records into a CALT dump body."""
    if isinstance(records, dict) and not any(
        k in records for k in ("records", "samples", "data")
    ):
        # Already typed buckets: {sleep: [...], steps: [...], ...}
        buckets = records
        flat: list[dict[str, Any]] = []
        for kind, items in buckets.items():
            if not isinstance(items, list):
                continue
            for item in items:
                if isinstance(item, dict):
                    flat.append({**item, "type": item.get("type") or kind})
        records = flat
    elif isinstance(records, dict):
        nested = records.get("records") or records.get("samples") or records.get("data")
        records = nested if isinstance(nested, list) else []

    sleep_recs: list[dict[str, Any]] = []
    steps = None
    kcal = None
    dist_m = None
    hr_last = None
    hr_resting = None
    spo2 = None
    stress = None
    hrv = None
    respiratory = None
    active_min = None
    workouts: list[dict[str, Any]] = []
    day = local_date

    for rec in records:
        if not isinstance(rec, dict):
            continue
        typ = str(rec.get("type") or rec.get("recordType") or rec.get("dataType") or "").lower()
        day = day or _iso_day(rec.get("local_date") or rec.get("date") or rec.get("start"))

        if "sleep" in typ:
            sleep_recs.append(rec)
        elif "step" in typ:
            steps = _as_int(rec.get("count") or rec.get("steps") or rec.get("value")) or steps
        elif "calorie" in typ or "energy" in typ:
            kcal = _as_int(rec.get("kcal") or rec.get("energy") or rec.get("value")) or kcal
        elif "distance" in typ:
            meters = rec.get("meters")
            if meters is None and rec.get("km") is not None:
                meters = float(rec["km"]) * 1000
            dist_m = _as_int(meters if meters is not None else rec.get("value")) or dist_m
        elif "resting" in typ and "heart" in typ:
            hr_resting = _as_int(rec.get("bpm") or rec.get("value")) or hr_resting
        elif "heart" in typ or typ in ("hr", "bpm"):
            hr_last = _as_int(rec.get("bpm") or rec.get("value")) or hr_last
        elif "oxygen" in typ or "spo2" in typ:
            spo2 = _as_int(rec.get("percentage") or rec.get("value")) or spo2
        elif "stress" in typ:
            stress = _as_int(rec.get("value") or rec.get("level")) or stress
        elif "hrv" in typ or "rmssd" in typ:
            hrv = {
                "rmssd_ms": _as_float(rec.get("rmssd_ms") or rec.get("rmssd") or rec.get("value")),
                "captured_at": rec.get("time") or rec.get("start"),
            }
        elif "respirat" in typ or "breath" in typ:
            respiratory = {
                "rate": _as_float(rec.get("rate") or rec.get("value")),
                "captured_at": rec.get("time") or rec.get("start"),
            }
        elif "exercise" in typ or "workout" in typ or "session" in typ:
            workouts.append(
                {
                    "type": rec.get("exercise_type") or rec.get("title") or typ,
                    "duration_min": _as_int(rec.get("duration_min") or rec.get("minutes")),
                    "calories": _as_int(rec.get("calories") or rec.get("kcal")),
                    "start": rec.get("start") or rec.get("start_time"),
                    "end": rec.get("end") or rec.get("end_time"),
                }
            )
        elif "active" in typ and "calor" not in typ:
            active_min = _as_int(rec.get("minutes") or rec.get("value")) or active_min

    body: dict[str, Any] = {
        "source": "health_connect",
        "dump": "health_connect_v1",
        "captured_at": datetime.now(timezone.utc).isoformat(),
    }
    if day:
        body["local_date"] = day

    sleep = _merge_sleep_from_hc(sleep_recs)
    if sleep:
        body["sleep"] = sleep
    if steps is not None:
        body["activity"] = {"steps": steps}
    if kcal is not None:
        body["calorie"] = {"kcal": kcal}
    if dist_m is not None:
        body["distance"] = {"meters": dist_m}
    heart: dict[str, Any] = {}
    if hr_last is not None:
        heart["last"] = hr_last
    if hr_resting is not None:
        heart["resting"] = hr_resting
    if heart:
        body["heart"] = heart
    if spo2 is not None:
        body["spo2"] = {"value": spo2}
    if stress is not None:
        body["stress"] = {"value": stress}
    if hrv and hrv.get("rmssd_ms") is not None:
        body["hrv"] = hrv
    if respiratory and respiratory.get("rate") is not None:
        body["respiratory"] = respiratory
    if workouts:
        body["workouts"] = [w for w in workouts if any(v is not None for k, v in w.items() if k != "type")]
    if active_min is not None:
        body["active_minutes"] = {"minutes": active_min}

    caps = {k: True for k in body if k in KNOWN_CATEGORIES}
    body["capabilities"] = caps
    return body


def coerce_ingest_body(raw: dict[str, Any]) -> dict[str, Any]:
    """If the client sent HC records (or a bridge export), expand into dump keys.

    Already-shaped CALT Sync bodies pass through with extras preserved.
    """
    if not isinstance(raw, dict):
        return {}
    out = dict(raw)
    source = str(out.get("source") or "").strip().lower()

    records = out.pop("health_connect_records", None)
    if records is None and source in ("health_connect", "hc", "google_fit"):
        records = out.get("records")

    # ZeppBridge-style daily export: { daily: { steps, sleep, ... }, workouts: [...] }
    if "daily" in out and isinstance(out["daily"], dict) and source in (
        "zeppbridge",
        "bridge_export",
        "health_connect",
        "",
    ):
        daily = out["daily"]
        if out.get("activity") is None and daily.get("steps") is not None:
            out["activity"] = {"steps": _as_int(daily.get("steps"))}
        if out.get("heart") is None and (
            daily.get("resting_hr") is not None or daily.get("hr_resting") is not None
        ):
            out["heart"] = {
                "resting": _as_int(daily.get("resting_hr") or daily.get("hr_resting")),
            }
        if out.get("spo2") is None and daily.get("spo2") is not None:
            out["spo2"] = {"value": _as_int(daily.get("spo2"))}
        if out.get("stress") is None and daily.get("stress") is not None:
            out["stress"] = {"value": _as_int(daily.get("stress"))}
        if out.get("pai") is None and daily.get("pai") is not None:
            out["pai"] = {"today": _as_float(daily.get("pai"))}
        if out.get("hrv") is None and daily.get("hrv") is not None:
            out["hrv"] = {"rmssd_ms": _as_float(daily.get("hrv"))}
        if out.get("respiratory") is None and daily.get("respiratory_rate") is not None:
            out["respiratory"] = {"rate": _as_float(daily.get("respiratory_rate"))}
        if out.get("vo2max") is None and daily.get("vo2max") is not None:
            out["vo2max"] = {"ml_kg_min": _as_float(daily.get("vo2max"))}
        if not out.get("source"):
            out["source"] = "bridge_export"

    if records is not None:
        mapped = normalize_health_connect_records(
            records,
            local_date=_iso_day(out.get("local_date")),
        )
        # Mapped scalars fill gaps; explicit top-level keys win.
        for key, val in mapped.items():
            if key in ("source", "dump", "captured_at", "capabilities"):
                if key not in out or out.get(key) in (None, "", {}):
                    out[key] = val
                continue
            if out.get(key) is None:
                out[key] = val
        if not out.get("source"):
            out["source"] = "health_connect"
        caps = dict(out.get("capabilities") or {})
        caps.update(mapped.get("capabilities") or {})
        out["capabilities"] = caps

    return out
