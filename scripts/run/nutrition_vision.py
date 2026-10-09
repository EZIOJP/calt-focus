"""Focus NutriNode photo vision + optional text estimate via Gemini.

Keys (never hardcode): GEMINI_API_KEY / LLM_CLOUD_API_KEY / LLM_API_KEY env,
optional behavior/nutrition_llm.json, or sibling Study .env for local use.
"""
from __future__ import annotations

import base64
import json
import os
import re
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[2]
BEHAVIOR = REPO / "data" / "productivity" / "behavior"
if not BEHAVIOR.is_dir():
    BEHAVIOR = REPO / "data" / "behavior"

LLM_CFG = BEHAVIOR / "nutrition" / "nutrition_llm.json"
STUDY_ENV = REPO.parent / "Cognitive-Aware Learning Tutor" / ".env"

GEMINI_API_BASE = "https://generativelanguage.googleapis.com/v1beta"
DEFAULT_MODEL = "gemini-3.8-flash"

_PHOTO_PROMPT = """You are a nutrition assistant looking at a plate or food photo.
Suggest discrete food items the user can confirm (do not invent macros).

Respond ONLY with valid JSON (no markdown):
{
  "items": [
    {
      "suggested_name": "<common food name>",
      "estimated_weight_g": <optional number or null>,
      "confidence": <0.0 to 1.0>
    }
  ],
  "description": "<one sentence>"
}
List 1-6 items. Prefer common Indian dish names when appropriate.
"""

_ESTIMATE_PROMPT = """You are a nutrition estimator for Indian and global home cooking.
Given a food name and portion weight in grams, estimate macros.

Respond ONLY with valid JSON (no markdown):
{
  "food_name": "<normalized lowercase name>",
  "per_100g": {
    "kcal": <number>,
    "protein_g": <number>,
    "carbs_g": <number>,
    "fat_g": <number>,
    "fiber_g": <number>
  },
  "confidence": <0.0 to 1.0>,
  "notes": "<one short sentence>"
}
"""


def _strip_placeholders(key: str) -> str:
    k = (key or "").strip()
    if not k:
        return ""
    if k.lower() in {"changeme", "your-key-here", "your-key-from-aistudio.google.com"}:
        return ""
    return k


def _parse_dotenv(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return out
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("#") or "=" not in s:
            continue
        if s.lower().startswith("export "):
            s = s[7:].strip()
        k, _, v = s.partition("=")
        k = k.strip()
        v = v.strip().strip('"').strip("'")
        if k:
            out[k] = v
    return out


def gemini_api_key() -> str:
    for env_name in ("GEMINI_API_KEY", "LLM_CLOUD_API_KEY", "LLM_API_KEY"):
        key = _strip_placeholders(os.environ.get(env_name, ""))
        if key:
            return key

    cfg = {}
    try:
        raw = LLM_CFG.read_text(encoding="utf-8")
        cfg = json.loads(raw) if raw.strip() else {}
    except (OSError, json.JSONDecodeError):
        cfg = {}
    if isinstance(cfg, dict):
        for field in ("gemini_api_key", "llm_cloud_api_key", "api_key"):
            key = _strip_placeholders(str(cfg.get(field) or ""))
            if key:
                return key

    if STUDY_ENV.is_file():
        env = _parse_dotenv(STUDY_ENV)
        for env_name in ("GEMINI_API_KEY", "LLM_CLOUD_API_KEY", "LLM_API_KEY"):
            key = _strip_placeholders(env.get(env_name, ""))
            if key:
                return key
    return ""


def vision_ready() -> bool:
    return bool(gemini_api_key())


def _strip_json(raw: str) -> str:
    text = (raw or "").strip()
    if text.startswith("```"):
        parts = text.split("```")
        text = parts[1] if len(parts) > 1 else text
        if text.startswith("json"):
            text = text[4:]
    text = text.strip()
    m = re.search(r"\{.*\}", text, re.DOTALL)
    return m.group(0) if m else text


def _parse_gemini_output(data: dict[str, Any]) -> str | None:
    candidates = data.get("candidates") or []
    if not candidates:
        return None
    content = candidates[0].get("content") or {}
    parts = content.get("parts") or []
    texts: list[str] = []
    for part in parts:
        if isinstance(part, dict):
            t = part.get("text")
            if isinstance(t, str) and t.strip():
                texts.append(t.strip())
    return "\n".join(texts) if texts else None


def _gemini_generate_parts(
    parts: list[dict[str, Any]],
    *,
    system_prompt: str | None = None,
    model: str | None = None,
    timeout: float = 90.0,
    max_tokens: int = 1024,
) -> str | None:
    api_key = gemini_api_key()
    if not api_key:
        return None

    model = (model or os.environ.get("NUTRITION_VISION_MODEL") or DEFAULT_MODEL).replace("models/", "")
    payload: dict[str, Any] = {
        "contents": [{"role": "user", "parts": parts}],
        "generationConfig": {
            "temperature": 0.2,
            "maxOutputTokens": max_tokens,
        },
    }
    if system_prompt:
        payload["systemInstruction"] = {"parts": [{"text": system_prompt}]}

    url = f"{GEMINI_API_BASE}/models/{model}:generateContent"
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "x-goog-api-key": api_key,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as res:
            body = json.loads(res.read().decode("utf-8"))
        return _parse_gemini_output(body)
    except urllib.error.HTTPError as exc:
        try:
            detail = exc.read().decode("utf-8", errors="replace")[:300]
        except Exception:
            detail = str(exc)
        print(f"[nutrition_vision] Gemini HTTP {exc.code}: {detail}", flush=True)
        return None
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
        print(f"[nutrition_vision] Gemini call failed: {exc}", flush=True)
        return None


def _decode_image_payload(body: dict[str, Any]) -> tuple[bytes, str]:
    """Accept image_b64 (+ mime) or raw image bytes already decoded by caller."""
    mime = str(body.get("mime") or body.get("mime_type") or "image/jpeg").strip().lower()
    if mime not in ("image/jpeg", "image/jpg", "image/png", "image/webp"):
        mime = "image/jpeg"
    if mime == "image/jpg":
        mime = "image/jpeg"

    b64 = body.get("image_b64") or body.get("image") or body.get("data")
    if isinstance(b64, str) and b64.strip():
        s = b64.strip()
        if "," in s and s.lower().startswith("data:"):
            s = s.split(",", 1)[1]
        try:
            raw = base64.standard_b64decode(s)
        except Exception as exc:
            raise ValueError("invalid image_b64") from exc
        if len(raw) < 32:
            raise ValueError("image too small")
        if len(raw) > 8_000_000:
            raise ValueError("image too large (max ~6MB)")
        return raw, mime

    raise ValueError("missing image_b64")


def analyze_photo(body: dict[str, Any]) -> dict[str, Any]:
    """Vision → suggested food names (user still confirms weight)."""
    import time

    t0 = time.perf_counter()
    if not gemini_api_key():
        raise RuntimeError(
            "Photo AI needs GEMINI_API_KEY or LLM_CLOUD_API_KEY "
            "(set in env, or data/productivity/behavior/nutrition/nutrition_llm.json)."
        )

    image_bytes, mime = _decode_image_payload(body)
    b64 = base64.standard_b64encode(image_bytes).decode("utf-8")
    raw = _gemini_generate_parts(
        [
            {"inlineData": {"mimeType": mime, "data": b64}},
            {"text": "Identify foods on this plate for logging."},
        ],
        system_prompt=_PHOTO_PROMPT,
        max_tokens=1024,
        timeout=90.0,
    )
    if not raw:
        raise RuntimeError("Photo analysis failed — check Gemini key / network (hub log has detail)")

    data = json.loads(_strip_json(raw))
    items: list[dict[str, Any]] = []
    for it in data.get("items") or []:
        if not isinstance(it, dict):
            continue
        name = str(it.get("suggested_name") or "").strip()
        if not name:
            continue
        weight = it.get("estimated_weight_g")
        items.append(
            {
                "suggested_name": name,
                "estimated_weight_g": float(weight) if weight is not None else None,
                "confidence": float(it.get("confidence") or 0.5),
            }
        )
    elapsed_ms = int((time.perf_counter() - t0) * 1000)
    return {
        "ok": True,
        "items": items,
        "description": data.get("description") or "",
        "source": "gemini_vision",
        "recognize_ms": elapsed_ms,
        "image_bytes": len(image_bytes),
        "mime": mime,
        "analyzed_at": __import__("datetime").datetime.now(__import__("datetime").timezone.utc).isoformat(),
    }


def estimate_nutrition_ai(food_name: str, weight_g: float) -> dict[str, Any] | None:
    """Optional Gemini text estimate when local/IFCT miss. Returns None if unavailable."""
    if not gemini_api_key() or not (food_name or "").strip():
        return None
    prompt = (
        f"Food: {food_name.strip()}\n"
        f"Portion weight: {float(weight_g):.1f} g\n"
        "Estimate typical cooked / as-eaten values."
    )
    raw = _gemini_generate_parts(
        [{"text": prompt}],
        system_prompt=_ESTIMATE_PROMPT,
        max_tokens=512,
    )
    if not raw:
        return None
    try:
        data = json.loads(_strip_json(raw))
    except json.JSONDecodeError:
        return None
    per_100 = data.get("per_100g") or {}
    per_g = {
        "kcal": float(per_100.get("kcal") or 0) / 100.0,
        "p": float(per_100.get("protein_g") or 0) / 100.0,
        "c": float(per_100.get("carbs_g") or 0) / 100.0,
        "f": float(per_100.get("fat_g") or 0) / 100.0,
        "fiber": float(per_100.get("fiber_g") or 0) / 100.0,
    }
    w = max(0.1, float(weight_g))
    return {
        "food_name": (data.get("food_name") or food_name).strip().lower(),
        "total_kcal": round(per_g["kcal"] * w, 1),
        "protein_g": round(per_g["p"] * w, 1),
        "carbs_g": round(per_g["c"] * w, 1),
        "fat_g": round(per_g["f"] * w, 1),
        "fiber_g": round(per_g["fiber"] * w, 1),
        "macros_source": "ai",
        "confidence": float(data.get("confidence") or 0.5),
        "notes": data.get("notes") or "",
        "per_g": per_g,
        "per_100g": {
            "kcal": round(per_g["kcal"] * 100, 1),
            "protein_g": round(per_g["p"] * 100, 1),
            "carbs_g": round(per_g["c"] * 100, 1),
            "fat_g": round(per_g["f"] * 100, 1),
            "fiber_g": round(per_g["fiber"] * 100, 1),
        },
    }
