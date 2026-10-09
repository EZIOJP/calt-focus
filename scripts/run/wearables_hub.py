#!/usr/bin/env python3
"""
Focus wearables ingest hub — Amazfit / CALT Sync (:8765).

Permanent sidecar (design lock): phone Zepp mini-program POSTs here.
Watch RTOS C++ cannot talk to calt_enforcer C++ — BLE → phone JS → HTTP only.

Also accepts google_fit / health_connect shaped bodies (see wearables_coerce.py).

  python scripts/run/wearables_hub.py
  scripts\\run\\start_wearables_hub.bat

Env:
  TRACKER_HUB_PORT   default 8765
  TRACKER_HUB_HOST   default 0.0.0.0
  WEARABLES_INGEST_KEY  default calt-local-wearables
"""
from __future__ import annotations

import json
import os
import secrets
import socket
import ssl
import sys
import threading
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

REPO = Path(__file__).resolve().parents[2]
STATIC_MOBILE = Path(__file__).resolve().parent / "static_mobile"
BEHAVIOR = REPO / "data" / "productivity" / "behavior"
if not BEHAVIOR.is_dir():
    BEHAVIOR = REPO / "data" / "behavior"

DAYS_DIR = BEHAVIOR / "wearable_days"
LIFE_DAYS_DIR = BEHAVIOR / "life_days"
SYNC_PATH = BEHAVIOR / "wearables_last_sync.json"
TODAY_MIRROR = BEHAVIOR / "wearable_day.json"
LIFE_TODAY = BEHAVIOR / "life_today.json"
PLAN_BLOCKS = BEHAVIOR / "plan_blocks.json"

_HUB_VERSION = "1.0.0-focus"
_WATCH_SOURCES = frozenset(
    {
        "mini_program",
        "zepp",
        "amazfit",
        "health_connect",
        "hc",
        "google_fit",
        "bridge_export",
        "zeppbridge",
    }
)

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from wearables_coerce import coerce_ingest_body, inventory_categories
except ImportError:  # pragma: no cover
    def coerce_ingest_body(raw: dict[str, Any]) -> dict[str, Any]:
        return dict(raw) if isinstance(raw, dict) else {}

    def inventory_categories(payload: dict[str, Any] | None) -> dict[str, Any]:
        return {"present": [], "count": 0, "details": {}, "capabilities": None}

try:
    from nutrition_focus import handle_nutrition
except ImportError:  # pragma: no cover
    def handle_nutrition(method, path, qs, body):  # type: ignore
        return 503, {"ok": False, "detail": "nutrition_focus missing"}


def hub_port() -> int:
    try:
        return max(1, int(os.environ.get("TRACKER_HUB_PORT", "8765")))
    except ValueError:
        return 8765


def hub_tls_port() -> int:
    """HTTPS port for Chrome camera (getUserMedia needs secure context)."""
    try:
        return max(1, int(os.environ.get("TRACKER_HUB_TLS_PORT", "8766")))
    except ValueError:
        return 8766


def hub_host() -> str:
    return (os.environ.get("TRACKER_HUB_HOST") or "0.0.0.0").strip() or "0.0.0.0"


def expected_key() -> str:
    return (os.environ.get("WEARABLES_INGEST_KEY") or "calt-local-wearables").strip()


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _today_local() -> str:
    return datetime.now().date().isoformat()


def _read_json(path: Path) -> dict[str, Any]:
    try:
        if path.is_file():
            data = json.loads(path.read_text(encoding="utf-8"))
            return data if isinstance(data, dict) else {}
    except Exception:
        pass
    return {}


def _write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp.replace(path)


def _lan_ips() -> list[str]:
    ips: list[str] = []
    try:
        hostname = socket.gethostname()
        for info in socket.getaddrinfo(hostname, None, socket.AF_INET):
            ip = info[4][0]
            if ip and not ip.startswith("127.") and ip not in ips:
                ips.append(ip)
    except Exception:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        if ip and not ip.startswith("127.") and ip not in ips:
            ips.insert(0, ip)
    except Exception:
        pass
    return ips


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


def _sleep_hours(sleep: Any) -> float | None:
    if not isinstance(sleep, dict):
        return None
    total = _as_int(sleep.get("total_min"))
    if total is None or total <= 0:
        return None
    return round(max(0.0, min(16.0, total / 60.0)), 2)


def _merge_day(prior: dict[str, Any], body: dict[str, Any]) -> dict[str, Any]:
    out = dict(prior or {})
    for key, value in (body or {}).items():
        if value is None:
            continue
        if key == "sleep" and isinstance(value, dict):
            total = _as_int(value.get("total_min"))
            if total is not None and total <= 0 and isinstance(out.get("sleep"), dict):
                continue
            merged = dict(out.get("sleep") or {}) if isinstance(out.get("sleep"), dict) else {}
            merged.update({k: v for k, v in value.items() if v is not None})
            out["sleep"] = merged
            continue
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            merged = dict(out[key])
            merged.update({k: v for k, v in value.items() if v is not None})
            out[key] = merged
        else:
            out[key] = value
    return out


def _day_summary(day: str, payload: dict[str, Any], sync: dict[str, Any]) -> dict[str, Any]:
    sleep = payload.get("sleep") if isinstance(payload.get("sleep"), dict) else {}
    activity = payload.get("activity") if isinstance(payload.get("activity"), dict) else {}
    heart = payload.get("heart") if isinstance(payload.get("heart"), dict) else {}
    calorie = payload.get("calorie") if isinstance(payload.get("calorie"), dict) else {}
    distance = payload.get("distance") if isinstance(payload.get("distance"), dict) else {}
    spo2 = payload.get("spo2") if isinstance(payload.get("spo2"), dict) else {}
    stress = payload.get("stress") if isinstance(payload.get("stress"), dict) else {}
    pai = payload.get("pai") if isinstance(payload.get("pai"), dict) else {}
    stand = payload.get("stand") if isinstance(payload.get("stand"), dict) else {}
    sitting = payload.get("sitting") if isinstance(payload.get("sitting"), dict) else {}
    battery = payload.get("battery") if isinstance(payload.get("battery"), dict) else {}
    hours = _sleep_hours(sleep)
    deep = _as_int(sleep.get("deep_min"))
    return {
        "local_date": day,
        "source": payload.get("source") or sync.get("last_source"),
        "synced_at": sync.get("last_ingest_at") or sync.get("updated_at"),
        "sleep_hours": hours,
        "sleep_score": _as_int(sleep.get("score")),
        "sleep_deep_min": deep,
        "sleep_label": f"{hours}h" if hours is not None else None,
        "sleep_deep_label": f"{deep}m" if deep is not None else None,
        "steps": _as_int(activity.get("steps")),
        "step_target": _as_int(activity.get("target")),
        "calories": _as_int(calorie.get("kcal")),
        "calorie_target": _as_int(calorie.get("target")),
        "distance_m": _as_int(distance.get("meters")),
        "hr_last": _as_int(heart.get("last")),
        "hr_resting": _as_int(heart.get("resting")),
        "spo2": _as_int(spo2.get("value") or spo2.get("last_day_avg")),
        "stress": _as_int(stress.get("value")),
        "pai_today": _as_float(pai.get("today")),
        "pai_total": _as_float(pai.get("total")),
        "stand_hours": _as_int(stand.get("hours")),
        "stand_target": _as_int(stand.get("target")),
        "battery_pct": _as_int(battery.get("pct")),
        "sitting_min": _as_int(
            sitting.get("minutes")
            or sitting.get("min")
            or sitting.get("sitting_min")
            or sitting.get("value")
            or activity.get("sitting_min")
        ),
        "tz_offset_min": payload.get("tz_offset_min"),
        "watch_local_date": payload.get("local_date"),
        "captured_at": payload.get("captured_at"),
        "last_captured_at": payload.get("captured_at"),
        "last_dump_id": payload.get("dump_id") or sync.get("last_dump_id"),
        "last_chunk_id": payload.get("chunk_id") or sync.get("last_chunk_id"),
        "last_checksum": payload.get("checksum"),
        "capabilities": payload.get("capabilities")
        if isinstance(payload.get("capabilities"), dict)
        else None,
        "payload": payload,
    }


def _patch_sync_from_day(sync: dict[str, Any], day: str, payload: dict[str, Any], *, duplicate: bool) -> dict[str, Any]:
    summary = _day_summary(day, payload, sync)
    source = str(payload.get("source") or "").strip().lower()
    is_watch = source in _WATCH_SOURCES
    sync.update(
        {
            "updated_at": _now_iso(),
            "last_ingest_at": _now_iso(),
            "last_event": "ingest",
            "last_source": source or "unknown",
            "last_is_watch": is_watch,
            "last_wrote_life": is_watch,
            "last_local_date": day,
            "last_dump_id": summary.get("last_dump_id"),
            "last_chunk_id": summary.get("last_chunk_id"),
            "last_duplicate": duplicate,
            "last_manual_dump": bool(payload.get("dump")),
            "last_sleep_hours": summary.get("sleep_hours"),
            "last_sleep_quality": summary.get("sleep_score"),
            "last_steps": summary.get("steps"),
            "last_step_target": summary.get("step_target"),
            "last_calories": summary.get("calories"),
            "last_distance_m": summary.get("distance_m"),
            "last_hr": summary.get("hr_last"),
            "last_spo2": summary.get("spo2"),
            "last_stress": summary.get("stress"),
            "last_pai": summary.get("pai_today"),
            "last_stand": summary.get("stand_hours"),
            "last_sitting_min": summary.get("sitting_min"),
            "last_battery": summary.get("battery_pct"),
            "last_tz_offset_min": summary.get("tz_offset_min"),
            "last_watch_local_date": summary.get("watch_local_date"),
            "last_captured_at": summary.get("captured_at"),
        }
    )
    return sync


def _load_day(day: str) -> dict[str, Any]:
    return _read_json(DAYS_DIR / f"{day}.json")


def _save_day(day: str, payload: dict[str, Any]) -> None:
    _write_json(DAYS_DIR / f"{day}.json", payload)


def _stress_to_life(stress: int | None) -> int | None:
    if stress is None or stress <= 0:
        return None
    return max(1, min(5, int(round(stress / 20)) or 1))


def _score_to_quality(score: int | None) -> int:
    if score is None:
        return 3
    return max(1, min(5, int(round(score / 20)) or 1))


def wearable_to_life(day: str, summary: dict[str, Any] | None, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    """Map Fit/HC/watch dump → Life Tracker daily row (Focus mirrors)."""
    s = summary or {}
    p = payload or {}
    steps = _as_int(s.get("steps"))
    exercise = min(180, int(steps) // 100) if steps is not None else None
    # Prefer workout/active_minutes when present
    workouts = p.get("workouts") if isinstance(p.get("workouts"), list) else []
    workout_min = 0
    for w in workouts:
        if isinstance(w, dict):
            workout_min += _as_int(w.get("duration_min") or w.get("minutes")) or 0
    active = p.get("active_minutes") if isinstance(p.get("active_minutes"), dict) else {}
    active_min = _as_int(active.get("minutes"))
    if workout_min > 0:
        exercise = workout_min
    elif active_min is not None:
        exercise = active_min
    sleep_hours = s.get("sleep_hours")
    sleep_score = _as_int(s.get("sleep_score"))
    stress = _as_int(s.get("stress"))
    empty = sleep_hours is None and steps is None and exercise is None and stress is None
    return {
        "date": day,
        "empty": empty,
        "source": s.get("source") or p.get("source") or "google_fit",
        "sleep_hours": sleep_hours if sleep_hours is not None else 0,
        "sleep_quality": _score_to_quality(sleep_score),
        "exercise_minutes": exercise if exercise is not None else 0,
        "water_glasses": 0,
        "meals_healthy": 0,
        "study_minutes": 0,
        "tasks_completed": 0,
        "deep_work_blocks": 0,
        "screen_time_hours": 0,
        "social_media_minutes": 0,
        "outdoor_minutes": 0,
        "mood_score": 3,
        "stress_level": _stress_to_life(stress) or 3,
        "meditation_minutes": 0,
        "steps": steps,
        "hr_last": s.get("hr_last"),
        "spo2": s.get("spo2"),
        "synced_at": s.get("synced_at"),
    }


def _save_life(day: str, life: dict[str, Any]) -> None:
    LIFE_DAYS_DIR.mkdir(parents=True, exist_ok=True)
    _write_json(LIFE_DAYS_DIR / f"{day}.json", life)
    if day == _today_local():
        _write_json(LIFE_TODAY, life)


def load_life(day: str) -> dict[str, Any]:
    path = LIFE_DAYS_DIR / f"{day}.json"
    life = _read_json(path)
    if life:
        return life
    # Derive on the fly from wearable dump
    payload = _load_day(day)
    sync = _read_json(SYNC_PATH)
    if payload:
        summary = _day_summary(day, payload, sync)
        return wearable_to_life(day, summary, payload)
    if day == _today_local():
        mirror = _read_json(TODAY_MIRROR)
        if mirror:
            return wearable_to_life(day, mirror, mirror.get("payload") if isinstance(mirror.get("payload"), dict) else {})
    return {"date": day, "empty": True}


def _auth_ok(handler: BaseHTTPRequestHandler) -> bool:
    expected = expected_key()
    provided = (handler.headers.get("X-CALT-Wearable-Key") or "").strip()
    if not provided:
        auth = (handler.headers.get("Authorization") or "").strip()
        parts = auth.split(None, 1)
        if len(parts) == 2 and parts[0].lower() == "bearer":
            provided = parts[1].strip()
    if not provided:
        return False
    try:
        return secrets.compare_digest(provided, expected)
    except Exception:
        return provided == expected


def _plans(horizon_hours: int = 24) -> list[dict[str, Any]]:
    raw = _read_json(PLAN_BLOCKS)
    blocks = raw.get("blocks") if isinstance(raw.get("blocks"), list) else []
    now = datetime.now(timezone.utc)
    until = now + timedelta(hours=max(1, horizon_hours))
    out: list[dict[str, Any]] = []
    for b in blocks:
        if not isinstance(b, dict):
            continue
        start_s = str(b.get("start_at") or "")
        try:
            start = datetime.fromisoformat(start_s.replace("Z", "+00:00"))
            if start.tzinfo is None:
                start = start.replace(tzinfo=timezone.utc)
        except ValueError:
            continue
        if start < now - timedelta(hours=1) or start > until:
            continue
        out.append(
            {
                "id": b.get("id"),
                "title": b.get("title") or "Block",
                "category": b.get("category") or "focus",
                "start_at": b.get("start_at"),
                "end_at": b.get("end_at"),
                "status": b.get("status") or "scheduled",
                "source": "focus_plan_blocks",
            }
        )
    out.sort(key=lambda x: str(x.get("start_at") or ""))
    return out[:40]


def build_status() -> dict[str, Any]:
    sync = _read_json(SYNC_PATH)
    day = str(sync.get("last_local_date") or _today_local())
    payload = _load_day(day)
    if not payload:
        today = _today_local()
        payload = _load_day(today)
        if payload:
            day = today
    wd = _day_summary(day, payload, sync) if payload else _read_json(TODAY_MIRROR) or None
    cats = inventory_categories(payload) if payload else {"present": [], "count": 0}
    source = str((sync.get("last_source") or "")).lower()
    is_watch = bool(sync.get("last_is_watch")) or source in _WATCH_SOURCES
    authentic = {
        "watch_ingest": is_watch and source not in ("web_test", ""),
        "wrote_life": bool(sync.get("last_wrote_life")),
        "plans_from_watch": False,
        "verdict": (
            "authentic_watch"
            if is_watch and source not in ("web_test", "")
            else ("web_or_test" if source == "web_test" else "waiting")
        ),
    }
    steps = sync.get("last_steps")
    exercise_est = None
    if isinstance(steps, (int, float)):
        exercise_est = min(180, int(steps) // 100)
    return {
        "ok": True,
        "reachable": True,
        "service": "calt.focus.wearables_hub",
        "version": _HUB_VERSION,
        "last_sync": sync or None,
        "wearable_day": wd,
        "categories": cats,
        "authentic": authentic,
        "estimates": {
            "steps_to_exercise": "steps // 100 (cap 180)",
            "exercise_from_last_steps": exercise_est,
            "distance_to_outdoor": "not mapped on Focus hub",
            "stress_to_life": "stress / 20 → 1–5 (Study Life Tracker)",
            "sleep_quality": "score / 20 → 1–5",
        },
        "applied_to_life": None,
        "storage": {
            "mirrors": str(BEHAVIOR),
            "days": str(DAYS_DIR),
            "sync": str(SYNC_PATH),
            "note": "Focus writes behavior mirrors only (no Study life_daily_log).",
        },
    }


_lock = threading.Lock()


def ingest(body: dict[str, Any]) -> dict[str, Any]:
    with _lock:
        coerced = coerce_ingest_body(body if isinstance(body, dict) else {})
        day = str(coerced.get("local_date") or _today_local())[:10]
        prior = _load_day(day)
        dump_id = coerced.get("dump_id")
        chunk_id = coerced.get("chunk_id") or (coerced.get("meta") or {}).get("chunk_id")
        checksum = coerced.get("checksum")
        duplicate = False
        if dump_id and chunk_id and prior:
            if (
                prior.get("dump_id") == dump_id
                and prior.get("chunk_id") == chunk_id
                and prior.get("checksum") == checksum
                and checksum
            ):
                duplicate = True
        merged = prior if duplicate else _merge_day(prior, coerced)
        if not duplicate:
            if dump_id:
                merged["dump_id"] = dump_id
            if chunk_id:
                merged["chunk_id"] = chunk_id
            if checksum:
                merged["checksum"] = checksum
            _save_day(day, merged)
        sync = _read_json(SYNC_PATH)
        sync = _patch_sync_from_day(sync, day, merged, duplicate=duplicate)
        _write_json(SYNC_PATH, sync)
        summary = _day_summary(day, merged, sync)
        _write_json(TODAY_MIRROR, summary)
        life = wearable_to_life(day, summary, merged)
        _save_life(day, life)
        hours = summary.get("sleep_hours")
        return {
            "ok": True,
            "duplicate": duplicate,
            "local_date": day,
            "source": merged.get("source"),
            "sleep": {"sleep_hours": hours, "score": summary.get("sleep_score")},
            "wearable_day": summary,
            "life": life,
            "last_sync": sync,
        }


class Handler(BaseHTTPRequestHandler):
    server_version = f"CALTFocusWearables/{_HUB_VERSION}"

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))

    def _cors(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Authorization, Content-Type, X-CALT-Wearable-Key")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")

    def _json(self, code: int, payload: Any) -> None:
        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self._cors()
        self.end_headers()
        self.wfile.write(raw)

    def _bytes(self, code: int, raw: bytes, content_type: str, *, cache: str = "no-cache") -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", cache)
        self._cors()
        self.end_headers()
        self.wfile.write(raw)

    def _serve_mobile(self, path: str) -> bool:
        """Phone Chrome NutriNode UI on LAN — /n and static assets."""
        p = path.rstrip("/") or "/"
        if p in ("/n", "/nutri", "/nutrition/app", "/mobile"):
            html = STATIC_MOBILE / "nutri.html"
            if not html.is_file():
                self._json(404, {"ok": False, "detail": "mobile UI missing"})
                return True
            self._bytes(200, html.read_bytes(), "text/html; charset=utf-8")
            return True
        if p == "/n/manifest.webmanifest":
            mf = STATIC_MOBILE / "manifest.webmanifest"
            if mf.is_file():
                self._bytes(200, mf.read_bytes(), "application/manifest+json; charset=utf-8", cache="max-age=300")
                return True
        if p == "/n/icon.svg":
            icon = STATIC_MOBILE / "icon.svg"
            if icon.is_file():
                self._bytes(200, icon.read_bytes(), "image/svg+xml; charset=utf-8", cache="max-age=86400")
                return True
        return False

    def _read_body(self, *, max_bytes: int = 200_000) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            return {}
        if length > max_bytes:
            raise ValueError("payload too large")
        raw = self.rfile.read(length)
        data = json.loads(raw.decode("utf-8"))
        return data if isinstance(data, dict) else {}

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        qs = parse_qs(parsed.query)

        if self._serve_mobile(parsed.path.split("?", 1)[0]):
            return

        if path in ("/", "/health"):
            ips = _lan_ips()
            port = hub_port()
            tls_port = hub_tls_port()
            phone_urls = [f"http://{ip}:{port}/n" for ip in ips[:4]]
            camera_urls = [f"https://127.0.0.1:{tls_port}/n"] + [
                f"https://{ip}:{tls_port}/n" for ip in ips[:4]
            ]
            self._json(
                200,
                {
                    "ok": True,
                    "service": "calt.focus.wearables_hub",
                    "version": _HUB_VERSION,
                    "port": port,
                    "tls_port": tls_port,
                    "lan_ips": ips,
                    "phone_nutri": phone_urls,
                    "camera_nutri": camera_urls,
                    "hint": (
                        "Windows Chrome webcam → https://127.0.0.1:8766/n "
                        "(accept cert once). Amazfit still uses http://:8765"
                    ),
                },
            )
            return

        # Life Tracker daily (Focus) — same shape as Study /api/life/daily/{day}
        if path.startswith("/api/life/daily"):
            day_part = path[len("/api/life/daily") :].lstrip("/") or "today"
            day = _today_local() if day_part in ("", "today") else day_part[:10]
            self._json(200, load_life(day))
            return

        # NutriNode (Focus-local meal log + food search)
        if path.startswith("/api/nutrition"):
            code, payload = handle_nutrition("GET", path, qs, None)
            self._json(code, payload)
            return

        if path.startswith("/api/wearables/zepp"):
            if not _auth_ok(self):
                self._json(401, {"ok": False, "detail": "Invalid wearable ingest key"})
                return

            if path.endswith("/health") or path == "/api/wearables/zepp/health":
                self._json(
                    200,
                    {
                        "ok": True,
                        "service": "wearables.zepp",
                        "hub": "focus",
                        "last_sync": _read_json(SYNC_PATH),
                    },
                )
                return

            if path.endswith("/status") or path == "/api/wearables/zepp/status":
                self._json(200, build_status())
                return

            if "/day/" in path:
                day = path.rsplit("/", 1)[-1][:10]
                payload = _load_day(day)
                sync = _read_json(SYNC_PATH)
                day_obj = _day_summary(day, payload, sync) if payload else None
                self._json(200, {"ok": True, "day": day_obj})
                return

            if path.endswith("/plans") or path == "/api/wearables/zepp/plans":
                try:
                    hours = int((qs.get("horizon_hours") or ["24"])[0])
                except ValueError:
                    hours = 24
                plans = _plans(hours)
                sync = _read_json(SYNC_PATH)
                sync["last_plans_at"] = _now_iso()
                sync["last_plan_count"] = len(plans)
                _write_json(SYNC_PATH, sync)
                self._json(200, {"ok": True, "plans": plans, "count": len(plans)})
                return

            self._json(404, {"ok": False, "detail": "unknown wearables route"})
            return

        self._json(404, {"ok": False, "detail": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        qs = parse_qs(parsed.query)

        if path.startswith("/api/nutrition"):
            # Photo analyze sends base64 JPEG/PNG — allow ~8MB JSON for that route only.
            max_bytes = 10_000_000 if path.rstrip("/").endswith("/analyze-photo") else 200_000
            try:
                body = self._read_body(max_bytes=max_bytes)
            except ValueError as e:
                self._json(413, {"ok": False, "detail": str(e)})
                return
            except json.JSONDecodeError:
                self._json(400, {"ok": False, "detail": "invalid JSON"})
                return
            code, payload = handle_nutrition("POST", path, qs, body)
            self._json(code, payload)
            return

        if path in ("/api/wearables/zepp", "/api/wearables/zepp/"):
            if not _auth_ok(self):
                self._json(401, {"ok": False, "detail": "Invalid wearable ingest key"})
                return
            try:
                body = self._read_body()
            except ValueError as e:
                self._json(413, {"ok": False, "detail": str(e)})
                return
            except json.JSONDecodeError:
                self._json(400, {"ok": False, "detail": "invalid JSON"})
                return
            try:
                result = ingest(body)
            except Exception as e:  # pragma: no cover
                self._json(500, {"ok": False, "detail": str(e)})
                return
            self._json(200, result)
            return

        self._json(404, {"ok": False, "detail": "not found"})

    def do_DELETE(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        qs = parse_qs(parsed.query)
        if path.startswith("/api/nutrition"):
            code, payload = handle_nutrition("DELETE", path, qs, None)
            self._json(code, payload)
            return
        self._json(404, {"ok": False, "detail": "not found"})


def _start_https(host: str, tls_port: int) -> ThreadingHTTPServer | None:
    try:
        from hub_tls import ensure_hub_cert
    except ImportError:
        print("  HTTPS camera port skipped (hub_tls missing)", flush=True)
        return None
    try:
        cert, key = ensure_hub_cert()
        https = ThreadingHTTPServer((host, tls_port), Handler)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(certfile=str(cert), keyfile=str(key))
        https.socket = ctx.wrap_socket(https.socket, server_side=True)
        t = threading.Thread(target=https.serve_forever, name="hub-https", daemon=True)
        t.start()
        return https
    except Exception as exc:
        print(f"  HTTPS camera port failed: {exc}", flush=True)
        return None


def main() -> int:
    BEHAVIOR.mkdir(parents=True, exist_ok=True)
    DAYS_DIR.mkdir(parents=True, exist_ok=True)
    LIFE_DAYS_DIR.mkdir(parents=True, exist_ok=True)
    host = hub_host()
    port = hub_port()
    tls_port = hub_tls_port()
    server = ThreadingHTTPServer((host, port), Handler)
    https = _start_https(host, tls_port)
    ips = _lan_ips()
    print(f"Focus wearables hub v{_HUB_VERSION} on http://{host}:{port}", flush=True)
    if https:
        print(f"  camera HTTPS     -> https://127.0.0.1:{tls_port}/n  (Windows Chrome — accept cert once)", flush=True)
        for ip in ips[:4]:
            print(f"  camera HTTPS LAN -> https://{ip}:{tls_port}/n", flush=True)
    print(f"  PC HTTP fallback -> http://127.0.0.1:{port}/n", flush=True)
    for ip in ips[:4]:
        print(f"  phone HTTP       -> http://{ip}:{port}/n", flush=True)
        print(f"  Amazfit Base URL -> http://{ip}:{port}", flush=True)
    print(f"  token -> {expected_key()}", flush=True)
    print(f"  mirrors -> {BEHAVIOR}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped", flush=True)
        if https:
            https.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
