"""CALT Sync dump inventory.

Watch payloads are already shaped (processed_v2). This module reports which
categories arrived. It does not translate Google Fit, Health Connect, or
bridge exports.
"""

from __future__ import annotations

from typing import Any


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

# Sources the hub will store. mini_program is the stamp on CALT Sync 4.3.
CALT_SYNC_SOURCES: frozenset[str] = frozenset(
    {
        "",
        "mini_program",
        "calt_sync",
        "amazfit_calt_sync",
        "calt_zepp",
        "calt_zepp_raw",
        "zepp",
        "amazfit",
    }
)

FOREIGN_HEALTH_SOURCES: frozenset[str] = frozenset(
    {
        "health_connect",
        "hc",
        "google_fit",
        "bridge_export",
        "zeppbridge",
    }
)


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


def foreign_health_source(raw: dict[str, Any] | None) -> str | None:
    """Name of a non-CALT health body, or None when the dump may be stored."""
    if not isinstance(raw, dict):
        return None
    source = str(raw.get("source") or "").strip().lower()
    if source in FOREIGN_HEALTH_SOURCES:
        return source
    if raw.get("health_connect_records") is not None:
        return "health_connect"
    if source not in CALT_SYNC_SOURCES:
        return source or "unknown"
    if (
        not source
        and isinstance(raw.get("daily"), dict)
        and not raw.get("dump")
        and not raw.get("sleep")
        and not raw.get("activity")
    ):
        return "bridge_export"
    return None


def coerce_ingest_body(raw: dict[str, Any]) -> dict[str, Any]:
    """Pass a CALT Sync body through. Drop leftover foreign record blobs."""
    if not isinstance(raw, dict):
        return {}
    out = dict(raw)
    out.pop("health_connect_records", None)
    return out
