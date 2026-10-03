"""Screenshot helpers for Focus QA (PIL ImageGrab)."""
from __future__ import annotations

from pathlib import Path
from typing import Optional


def capture_screen(dest: Path, label: str = "shot") -> Optional[str]:
    """Grab full desktop; return relative path string or None."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        from PIL import ImageGrab  # type: ignore
    except ImportError:
        return None
    try:
        img = ImageGrab.grab()
        # Shrink for report size
        w, h = img.size
        if w > 1600:
            img = img.resize((1600, int(h * 1600 / w)))
        img.save(dest, format="PNG", optimize=True)
        return str(dest)
    except Exception:
        return None


def shot_path(artifact_dir: Path, case_id: str, suffix: str = "fail") -> Path:
    safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in case_id)[:80]
    return artifact_dir / "screenshots" / f"{safe}_{suffix}.png"
