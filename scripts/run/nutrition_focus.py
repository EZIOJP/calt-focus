"""Focus NutriNode — meal log + local food search (no Study :8000 required)."""
from __future__ import annotations

import json
import re
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs
from zoneinfo import ZoneInfo

REPO = Path(__file__).resolve().parents[2]
BEHAVIOR = REPO / "data" / "productivity" / "behavior"
if not BEHAVIOR.is_dir():
    BEHAVIOR = REPO / "data" / "behavior"

NUTRI_DIR = BEHAVIOR / "nutrition"
CUSTOM_PATH = NUTRI_DIR / "custom_foods.json"
EVENTS_PATH = NUTRI_DIR / "nutrition_events.jsonl"
LOCAL_TZ_NAME = "Asia/Kolkata"
# Optional: reuse Study IFCT if sibling checkout exists
STUDY_IFCT = REPO.parent / "Cognitive-Aware Learning Tutor" / "data" / "nutrition" / "ifct2017_compositions.csv"

LOCAL_MACRO_DB: dict[str, dict[str, float]] = {
    "chicken biryani": {"kcal": 1.85, "p": 0.10, "c": 0.18, "f": 0.08, "fiber": 0.01},
    "vegetable biryani": {"kcal": 1.50, "p": 0.04, "c": 0.22, "f": 0.05, "fiber": 0.02},
    "masala dosa": {"kcal": 1.97, "p": 0.04, "c": 0.25, "f": 0.08, "fiber": 0.01},
    "plain dosa": {"kcal": 1.68, "p": 0.04, "c": 0.27, "f": 0.04, "fiber": 0.01},
    "idli": {"kcal": 0.58, "p": 0.02, "c": 0.12, "f": 0.00, "fiber": 0.01},
    "sambar": {"kcal": 0.52, "p": 0.03, "c": 0.08, "f": 0.01, "fiber": 0.02},
    "dal tadka": {"kcal": 0.99, "p": 0.07, "c": 0.12, "f": 0.03, "fiber": 0.04},
    "paneer butter masala": {"kcal": 1.50, "p": 0.09, "c": 0.07, "f": 0.10, "fiber": 0.01},
    "roti": {"kcal": 2.97, "p": 0.09, "c": 0.53, "f": 0.04, "fiber": 0.05},
    "chapati": {"kcal": 2.97, "p": 0.09, "c": 0.53, "f": 0.04, "fiber": 0.05},
    "naan": {"kcal": 3.10, "p": 0.09, "c": 0.56, "f": 0.07, "fiber": 0.02},
    "rice (cooked)": {"kcal": 1.30, "p": 0.03, "c": 0.28, "f": 0.00, "fiber": 0.00},
    "cooked rice": {"kcal": 1.30, "p": 0.03, "c": 0.28, "f": 0.00, "fiber": 0.00},
    "chole": {"kcal": 1.64, "p": 0.09, "c": 0.27, "f": 0.03, "fiber": 0.08},
    "palak paneer": {"kcal": 1.25, "p": 0.08, "c": 0.05, "f": 0.09, "fiber": 0.02},
    "aloo gobi": {"kcal": 0.85, "p": 0.02, "c": 0.12, "f": 0.04, "fiber": 0.02},
    "poha": {"kcal": 1.80, "p": 0.03, "c": 0.34, "f": 0.05, "fiber": 0.01},
    "upma": {"kcal": 1.50, "p": 0.04, "c": 0.27, "f": 0.04, "fiber": 0.02},
    "egg": {"kcal": 1.55, "p": 0.13, "c": 0.01, "f": 0.11, "fiber": 0.00},
    "omelette": {"kcal": 1.54, "p": 0.10, "c": 0.01, "f": 0.12, "fiber": 0.00},
    "apple": {"kcal": 0.52, "p": 0.00, "c": 0.14, "f": 0.00, "fiber": 0.02},
    "banana": {"kcal": 0.89, "p": 0.01, "c": 0.23, "f": 0.00, "fiber": 0.03},
    "salad": {"kcal": 0.20, "p": 0.01, "c": 0.03, "f": 0.00, "fiber": 0.02},
    "burger": {"kcal": 2.50, "p": 0.12, "c": 0.25, "f": 0.13, "fiber": 0.01},
    "pizza": {"kcal": 2.66, "p": 0.11, "c": 0.33, "f": 0.10, "fiber": 0.02},
    "french fries": {"kcal": 3.12, "p": 0.04, "c": 0.41, "f": 0.15, "fiber": 0.04},
    "coffee": {"kcal": 0.02, "p": 0.00, "c": 0.00, "f": 0.00, "fiber": 0.00},
    "milk tea": {"kcal": 0.40, "p": 0.02, "c": 0.05, "f": 0.02, "fiber": 0.00},
    "curd": {"kcal": 0.60, "p": 0.04, "c": 0.05, "f": 0.03, "fiber": 0.00},
    "unknown": {"kcal": 1.50, "p": 0.05, "c": 0.20, "f": 0.07, "fiber": 0.02},
}

DEFAULT_SERVING_G = {
    "roti": 40.0,
    "chapati": 40.0,
    "idli": 40.0,
    "egg": 50.0,
    "banana": 120.0,
    "apple": 150.0,
}

_IFCT_CACHE: list[dict[str, Any]] | None = None


def _norm(s: str) -> str:
    return re.sub(r"\s+", " ", (s or "").strip().lower())


def _today() -> str:
    return datetime.now().date().isoformat()


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _now_local() -> datetime:
    try:
        return datetime.now(ZoneInfo(LOCAL_TZ_NAME))
    except Exception:
        return datetime.now().astimezone()


def _local_stamp(dt: datetime | None = None) -> dict[str, Any]:
    local = dt or _now_local()
    if local.tzinfo is None:
        local = local.astimezone()
    return {
        "timestamp": local.astimezone(timezone.utc).isoformat(),
        "local_time": local.isoformat(timespec="seconds"),
        "local_clock": local.strftime("%Y-%m-%d %H:%M:%S"),
        "local_date": local.date().isoformat(),
        "tz": str(local.tzinfo or LOCAL_TZ_NAME),
        "tz_offset_min": int(local.utcoffset().total_seconds() // 60) if local.utcoffset() else 0,
        "weekday": local.strftime("%A"),
    }


def _append_event(kind: str, payload: dict[str, Any]) -> None:
    """Append-only timeline for later review (one JSON object per line)."""
    try:
        NUTRI_DIR.mkdir(parents=True, exist_ok=True)
        row = {"event": kind, **_local_stamp(), **payload}
        with EVENTS_PATH.open("a", encoding="utf-8") as f:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")
    except OSError:
        pass


def _read_json(path: Path) -> Any:
    try:
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        pass
    return None


def _write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp.replace(path)


def _day_path(day: str) -> Path:
    return NUTRI_DIR / "days" / f"{day}.json"


def _load_day(day: str) -> dict[str, Any]:
    raw = _read_json(_day_path(day))
    if isinstance(raw, dict) and isinstance(raw.get("meals"), list):
        return raw
    return {"date": day, "meals": []}


def _totals(meals: list[dict[str, Any]]) -> dict[str, Any]:
    t = {
        "total_kcal": 0.0,
        "protein_g": 0.0,
        "carbs_g": 0.0,
        "fat_g": 0.0,
        "fiber_g": 0.0,
        "meal_count": len(meals),
    }
    for m in meals:
        t["total_kcal"] += float(m.get("total_kcal") or 0)
        t["protein_g"] += float(m.get("protein_g") or 0)
        t["carbs_g"] += float(m.get("carbs_g") or 0)
        t["fat_g"] += float(m.get("fat_g") or 0)
        t["fiber_g"] += float(m.get("fiber_g") or 0)
    for k in ("total_kcal", "protein_g", "carbs_g", "fat_g", "fiber_g"):
        t[k] = round(t[k], 1)
    return t


def _save_day(day: str, meals: list[dict[str, Any]]) -> dict[str, Any]:
    payload = {"date": day, "meals": meals, "totals": _totals(meals), "updated_at": _now_iso()}
    _write_json(_day_path(day), payload)
    if day == _today():
        _write_json(BEHAVIOR / "nutrition_today.json", payload)
    return payload


def today_payload(day: str | None = None) -> dict[str, Any]:
    d = day or _today()
    data = _load_day(d)
    meals = data.get("meals") or []
    return {"ok": True, "date": d, "meals": meals, "totals": _totals(meals)}


def macros_for_weight(per_g: dict[str, float], weight_g: float) -> dict[str, float]:
    w = max(0.0, float(weight_g))
    return {
        "total_kcal": round(w * float(per_g.get("kcal") or 0), 1),
        "protein_g": round(w * float(per_g.get("p") or 0), 1),
        "carbs_g": round(w * float(per_g.get("c") or 0), 1),
        "fat_g": round(w * float(per_g.get("f") or 0), 1),
        "fiber_g": round(w * float(per_g.get("fiber") or 0), 1),
    }


def _load_custom() -> list[dict[str, Any]]:
    raw = _read_json(CUSTOM_PATH)
    return raw if isinstance(raw, list) else []


def _load_ifct_lite(limit: int = 800) -> list[dict[str, Any]]:
    """Lazy-load a capped IFCT subset from Study sibling (optional)."""
    global _IFCT_CACHE
    if _IFCT_CACHE is not None:
        return _IFCT_CACHE
    out: list[dict[str, Any]] = []
    path = STUDY_IFCT
    if not path.is_file():
        _IFCT_CACHE = out
        return out
    try:
        import csv

        with path.open("r", encoding="utf-8", errors="replace", newline="") as f:
            reader = csv.reader(f)
            headers = next(reader, None)
            if not headers:
                _IFCT_CACHE = out
                return out

            def col(*names: str) -> int | None:
                for i, h in enumerate(headers):
                    code = h.replace('"', "").split(";")[-1].strip().lower()
                    if code in names:
                        return i
                return None

            i_name = col("name")
            i_grup = col("grup")
            i_enerc = col("enerc")
            i_pro = col("protcnt")
            i_cho = col("choavldf")
            i_fat = col("fatce")
            i_fib = col("fibtg")
            if i_name is None or i_enerc is None:
                _IFCT_CACHE = out
                return out
            for row in reader:
                if len(out) >= limit:
                    break
                if i_name >= len(row):
                    continue
                name = (row[i_name] or "").strip()
                if not name:
                    continue
                try:
                    # IFCT energy often in kJ — convert loosely when large
                    en = float(row[i_enerc] or 0) if i_enerc < len(row) else 0.0
                    kcal_100 = en / 4.184 if en > 400 else en
                    p = float(row[i_pro] or 0) if i_pro is not None and i_pro < len(row) else 0.0
                    c = float(row[i_cho] or 0) if i_cho is not None and i_cho < len(row) else 0.0
                    fat = float(row[i_fat] or 0) if i_fat is not None and i_fat < len(row) else 0.0
                    fib = float(row[i_fib] or 0) if i_fib is not None and i_fib < len(row) else 0.0
                except ValueError:
                    continue
                out.append(
                    {
                        "id": f"ifct:{_norm(name)}",
                        "name": name,
                        "name_key": _norm(name),
                        "group": (row[i_grup] if i_grup is not None and i_grup < len(row) else "")
                        or "IFCT",
                        "source": "ifct",
                        "per_g": {
                            "kcal": kcal_100 / 100.0,
                            "p": p / 100.0,
                            "c": c / 100.0,
                            "f": fat / 100.0,
                            "fiber": fib / 100.0,
                        },
                        "default_serving_g": 100.0,
                    }
                )
    except Exception:
        out = []
    _IFCT_CACHE = out
    return out


def food_index() -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for c in _load_custom():
        out.append(
            {
                "id": f"custom:{c.get('name')}",
                "name": c.get("display_name") or c.get("name"),
                "name_key": _norm(str(c.get("name") or "")),
                "group": "Custom",
                "source": "custom",
                "per_g": c.get("per_g") or {},
                "default_serving_g": float(c.get("default_serving_g") or 100),
            }
        )
    for name, m in LOCAL_MACRO_DB.items():
        if name == "unknown":
            continue
        out.append(
            {
                "id": f"local:{name}",
                "name": name.title(),
                "name_key": name,
                "group": "Local favourites",
                "source": "local",
                "per_g": m,
                "default_serving_g": float(DEFAULT_SERVING_G.get(name, 100)),
            }
        )
    out.extend(_load_ifct_lite())
    return out


def search_foods(q: str, limit: int = 20) -> list[dict[str, Any]]:
    query = _norm(q)
    if not query:
        return []
    tokens = query.split()
    scored: list[tuple[int, dict[str, Any]]] = []
    for food in food_index():
        key = food["name_key"]
        if query not in key and not all(t in key for t in tokens):
            continue
        score = 0
        if key.startswith(query):
            score += 100
        if query == key:
            score += 200
        if query in key:
            score += 50
        if food["source"] == "custom":
            score += 40
        elif food["source"] == "local":
            score += 20
        scored.append((score, food))
    scored.sort(key=lambda x: (-x[0], x[1]["name"]))
    results = []
    for _, food in scored[:limit]:
        pg = food["per_g"]
        results.append(
            {
                "id": food["id"],
                "name": food["name"],
                "group": food["group"],
                "source": food["source"],
                "default_serving_g": food["default_serving_g"],
                "per_100g": {
                    "kcal": round(float(pg.get("kcal") or 0) * 100, 1),
                    "protein_g": round(float(pg.get("p") or 0) * 100, 1),
                    "carbs_g": round(float(pg.get("c") or 0) * 100, 1),
                    "fat_g": round(float(pg.get("f") or 0) * 100, 1),
                    "fiber_g": round(float(pg.get("fiber") or 0) * 100, 1),
                },
            }
        )
    return results


def find_food(name: str) -> dict[str, Any] | None:
    key = _norm(name)
    best = None
    for food in food_index():
        if food["name_key"] == key:
            return food
        if key in food["name_key"]:
            best = best or food
    return best


def estimate(food_name: str, weight_g: float, ai_per_g: dict[str, float] | None = None) -> dict[str, Any]:
    food = find_food(food_name)
    if food:
        macros = macros_for_weight(food["per_g"], weight_g)
        return {
            "food_name": food["name"],
            **macros,
            "macros_source": food["source"],
            "confidence": 0.85,
            "per_g": food["per_g"],
            "per_100g": {
                "kcal": round(float(food["per_g"].get("kcal") or 0) * 100, 1),
                "protein_g": round(float(food["per_g"].get("p") or 0) * 100, 1),
                "carbs_g": round(float(food["per_g"].get("c") or 0) * 100, 1),
                "fat_g": round(float(food["per_g"].get("f") or 0) * 100, 1),
                "fiber_g": round(float(food["per_g"].get("fiber") or 0) * 100, 1),
            },
        }
    if ai_per_g:
        macros = macros_for_weight(ai_per_g, weight_g)
        return {
            "food_name": food_name,
            **macros,
            "macros_source": "ai",
            "confidence": 0.5,
            "per_g": ai_per_g,
        }
    try:
        from nutrition_vision import estimate_nutrition_ai

        ai = estimate_nutrition_ai(food_name, weight_g)
        if ai:
            return ai
    except Exception:
        pass
    macros = macros_for_weight(LOCAL_MACRO_DB["unknown"], weight_g)
    return {
        "food_name": food_name,
        **macros,
        "macros_source": "fallback",
        "confidence": 0.2,
        "notes": "Unknown food — using generic macros. Save a custom food to improve.",
        "per_g": LOCAL_MACRO_DB["unknown"],
    }


def save_custom(entry: dict[str, Any]) -> dict[str, Any]:
    rows = _load_custom()
    name = _norm(str(entry.get("name") or ""))
    per_g = entry.get("per_g") or {}
    row = {
        "name": name,
        "display_name": entry.get("display_name") or name,
        "per_g": {
            "kcal": float(per_g.get("kcal") or 0),
            "p": float(per_g.get("p") or 0),
            "c": float(per_g.get("c") or 0),
            "f": float(per_g.get("f") or 0),
            "fiber": float(per_g.get("fiber") or 0),
        },
        "default_serving_g": float(entry.get("default_serving_g") or 100),
        "source": "custom",
    }
    rows = [r for r in rows if _norm(str(r.get("name") or "")) != name]
    rows.append(row)
    _write_json(CUSTOM_PATH, rows)
    return row


def _clean_meta(raw: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(raw, dict):
        return {}
    out: dict[str, Any] = {}
    str_keys = (
        "client",
        "capture",
        "camera_label",
        "ua_hint",
        "notes",
        "photo_description",
        "logged_via",
    )
    for k in str_keys:
        v = raw.get(k)
        if isinstance(v, str) and v.strip():
            out[k] = v.strip()[:240]
    for k in ("recognize_ms", "capture_ms", "prepare_ms", "log_ms", "total_ms", "image_bytes"):
        try:
            if raw.get(k) is not None:
                out[k] = int(raw[k])
        except (TypeError, ValueError):
            pass
    if isinstance(raw.get("suggested_names"), list):
        out["suggested_names"] = [str(x)[:80] for x in raw["suggested_names"][:8] if x]
    try:
        if raw.get("photo_confidence") is not None:
            out["photo_confidence"] = round(float(raw["photo_confidence"]), 3)
    except (TypeError, ValueError):
        pass
    return out


def _meal_row(
    food_name: str,
    weight_g: float,
    *,
    meal_type: str = "lunch",
    servings: float = 1.0,
    macros_source: str | None = None,
    ai_per_g: dict[str, float] | None = None,
    meta: dict[str, Any] | None = None,
) -> dict[str, Any]:
    t0 = time.perf_counter()
    est = estimate(food_name, weight_g, ai_per_g)
    estimate_ms = int((time.perf_counter() - t0) * 1000)
    stamp = _local_stamp()
    extra = _clean_meta(meta)
    timings = {
        "estimate_ms": estimate_ms,
        "recognize_ms": extra.pop("recognize_ms", None),
        "capture_ms": extra.pop("capture_ms", None),
        "prepare_ms": extra.pop("prepare_ms", None),
        "log_ms": extra.pop("log_ms", None),
        "total_ms": extra.pop("total_ms", None),
    }
    timings = {k: v for k, v in timings.items() if v is not None}
    return {
        "meal_id": str(uuid.uuid4()),
        "timestamp": stamp["timestamp"],
        "local_time": stamp["local_time"],
        "local_clock": stamp["local_clock"],
        "local_date": stamp["local_date"],
        "tz": stamp["tz"],
        "tz_offset_min": stamp["tz_offset_min"],
        "weekday": stamp["weekday"],
        "food_item": est.get("food_name") or food_name,
        "weight_g": round(float(weight_g), 1),
        "servings": servings,
        "meal_type": meal_type,
        "total_kcal": est["total_kcal"],
        "protein_g": est["protein_g"],
        "carbs_g": est["carbs_g"],
        "fat_g": est["fat_g"],
        "fiber_g": est["fiber_g"],
        "confidence": float(est.get("confidence") or 0.5),
        "is_healthy": None,
        "location_tag": extra.get("client") or "focus",
        "source": "nutrinode",
        "macros_source": macros_source or est.get("macros_source"),
        "capture": extra.get("capture") or "manual",
        "client": extra.get("client") or "unknown",
        "timings": timings,
        "meta": extra,
    }


def log_manual(food_item: str, weight_grams: float, location_tag: str = "manual") -> dict[str, Any]:
    day = _today()
    data = _load_day(day)
    meals = list(data.get("meals") or [])
    row = _meal_row(food_item, weight_grams, meal_type="snack")
    row["location_tag"] = location_tag
    meals.insert(0, row)
    saved = _save_day(day, meals)
    return {"ok": True, "meal": row, "totals": saved["totals"]}


def confirm_meals(
    items: list[dict[str, Any]],
    meal_type: str = "lunch",
    *,
    session_meta: dict[str, Any] | None = None,
) -> dict[str, Any]:
    day = _today()
    data = _load_day(day)
    meals = list(data.get("meals") or [])
    added = []
    base_meta = _clean_meta(session_meta)
    for it in items:
        if not isinstance(it, dict):
            continue
        name = str(it.get("food_name") or "").strip()
        if not name:
            continue
        w = float(it.get("weight_g") or 100)
        servings = float(it.get("servings") or 1)
        item_meta = {**base_meta, **_clean_meta(it.get("meta") if isinstance(it.get("meta"), dict) else None)}
        row = _meal_row(
            name,
            w,
            meal_type=meal_type or "lunch",
            servings=servings,
            macros_source=it.get("macros_source"),
            ai_per_g=it.get("ai_per_g") if isinstance(it.get("ai_per_g"), dict) else None,
            meta=item_meta,
        )
        meals.insert(0, row)
        added.append(row)
        _append_event(
            "meal_logged",
            {
                "meal_id": row["meal_id"],
                "food_item": row["food_item"],
                "weight_g": row["weight_g"],
                "meal_type": row["meal_type"],
                "total_kcal": row["total_kcal"],
                "macros_source": row.get("macros_source"),
                "capture": row.get("capture"),
                "client": row.get("client"),
                "timings": row.get("timings"),
            },
        )
    saved = _save_day(day, meals)
    return {"ok": True, "meals": added, "totals": saved["totals"], "logged_at": _local_stamp()}


def delete_meal(meal_id: str) -> dict[str, Any]:
    day = _today()
    data = _load_day(day)
    removed = next((m for m in (data.get("meals") or []) if str(m.get("meal_id")) == str(meal_id)), None)
    meals = [m for m in (data.get("meals") or []) if str(m.get("meal_id")) != str(meal_id)]
    saved = _save_day(day, meals)
    _append_event(
        "meal_deleted",
        {
            "meal_id": meal_id,
            "food_item": (removed or {}).get("food_item"),
            "total_kcal": (removed or {}).get("total_kcal"),
        },
    )
    return {"ok": True, "totals": saved["totals"], "deleted_at": _local_stamp()}


def handle_nutrition(method: str, path: str, qs: dict[str, list[str]], body: dict[str, Any] | None) -> tuple[int, Any]:
    """Dispatch /api/nutrition/* for Focus hub. Returns (status, json_payload)."""
    body = body or {}
    p = path.rstrip("/") or "/"

    if method == "GET" and p.endswith("/today"):
        return 200, today_payload()

    if method == "GET" and "/foods/search" in p:
        q = (qs.get("q") or [""])[0]
        return 200, {"ok": True, "results": search_foods(q)}

    if method == "POST" and p.endswith("/foods/estimate"):
        return 200, estimate(str(body.get("food_name") or ""), float(body.get("weight_g") or 100))

    if method == "POST" and p.endswith("/foods/custom"):
        return 200, {"ok": True, "food": save_custom(body)}

    if method == "POST" and p.endswith("/manual"):
        return 200, log_manual(
            str(body.get("food_item") or ""),
            float(body.get("weight_grams") or body.get("weight_g") or 100),
            str(body.get("location_tag") or "manual"),
        )

    if method == "POST" and p.endswith("/meals"):
        items = body.get("items") if isinstance(body.get("items"), list) else []
        session_meta = body.get("meta") if isinstance(body.get("meta"), dict) else None
        return 200, confirm_meals(
            items,
            str(body.get("meal_type") or "lunch"),
            session_meta=session_meta,
        )

    if method == "GET" and p.endswith("/events"):
        # Last N timeline rows for debugging / later sense-making
        try:
            limit = int((qs.get("limit") or ["40"])[0])
        except ValueError:
            limit = 40
        limit = max(1, min(limit, 200))
        rows: list[dict[str, Any]] = []
        if EVENTS_PATH.is_file():
            try:
                lines = EVENTS_PATH.read_text(encoding="utf-8").splitlines()
                for line in lines[-limit:]:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        obj = json.loads(line)
                        if isinstance(obj, dict):
                            rows.append(obj)
                    except json.JSONDecodeError:
                        continue
            except OSError:
                pass
        return 200, {"ok": True, "events": rows, "path": str(EVENTS_PATH)}

    if method == "DELETE" and "/meals/" in p:
        meal_id = p.rsplit("/", 1)[-1]
        return 200, delete_meal(meal_id)

    if method == "POST" and p.endswith("/analyze-photo"):
        try:
            from nutrition_vision import analyze_photo, vision_ready

            if not vision_ready():
                return 503, {
                    "ok": False,
                    "detail": (
                        "Photo AI needs GEMINI_API_KEY or LLM_CLOUD_API_KEY "
                        "(env, or behavior/nutrition/nutrition_llm.json)."
                    ),
                }
            result = analyze_photo(body)
            _append_event(
                "photo_recognized",
                {
                    "item_count": len(result.get("items") or []),
                    "names": [i.get("suggested_name") for i in (result.get("items") or [])[:6]],
                    "description": str(result.get("description") or "")[:200],
                    "recognize_ms": result.get("recognize_ms"),
                    "image_bytes": result.get("image_bytes"),
                    "mime": result.get("mime"),
                    "source": result.get("source"),
                },
            )
            return 200, result
        except ValueError as e:
            _append_event("photo_failed", {"detail": str(e)[:200], "stage": "decode"})
            return 400, {"ok": False, "detail": str(e)}
        except RuntimeError as e:
            _append_event("photo_failed", {"detail": str(e)[:200], "stage": "runtime"})
            return 503, {"ok": False, "detail": str(e)}
        except Exception as e:
            _append_event("photo_failed", {"detail": str(e)[:200], "stage": "exception"})
            return 502, {"ok": False, "detail": f"Photo analysis failed: {e}"}

    if method == "POST" and "/pipeline/" in p:
        return 501, {"ok": False, "detail": "Nutrition pipeline is Study-only for now."}

    if method == "GET" and p.endswith("/nutrition"):
        try:
            from nutrition_vision import vision_ready

            photo_ai = vision_ready()
        except Exception:
            photo_ai = False
        return 200, {
            "ok": True,
            "service": "calt.focus.nutrinode",
            "photo_ai": photo_ai,
            "today": today_payload(),
        }

    return 404, {"ok": False, "detail": f"unknown nutrition route {p}"}
