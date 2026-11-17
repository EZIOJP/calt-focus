"""Multi-point gaze/posture + sit/stand baseline calibration + EMA smoothing."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any


class Ema:
    def __init__(self, alpha: float = 0.35) -> None:
        self.alpha = max(0.05, min(1.0, alpha))
        self.v: float | None = None

    def update(self, x: float | None) -> float | None:
        if x is None:
            return self.v
        if self.v is None:
            self.v = float(x)
        else:
            self.v = self.alpha * float(x) + (1.0 - self.alpha) * self.v
        return self.v

    def reset(self) -> None:
        self.v = None


def _ema_field(data: dict[str, Any], key: str, value: float | None, w: float) -> None:
    if value is None:
        return
    prev = data.get(key)
    if prev is None:
        data[key] = float(value)
    else:
        data[key] = (1.0 - w) * float(prev) + w * float(value)


class CalibrationStore:
    """Neutral head pose + face-width + dual sit/stand upper-body baselines.

    Sit/stand uses normalized landmark Y (0=top) and face scale — laptop cams rarely
    see hips, so we do not rely on real depth / ToF.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.data: dict[str, Any] = {
            "version": 2,
            "yaw0": 0.0,
            "pitch0": 0.0,
            "roll0": 0.0,
            "face_width0": None,
            "ear0": None,
            "samples": 0,
            # Sit baseline (normalized nose/shoulder Y + face width / IPD)
            "sit_samples": 0,
            "sit_nose_y": None,
            "sit_shoulder_y": None,
            "sit_face_width": None,
            "sit_ipd": None,
            # Stand baseline
            "stand_samples": 0,
            "stand_nose_y": None,
            "stand_shoulder_y": None,
            "stand_face_width": None,
            "stand_ipd": None,
        }
        self.load()

    def load(self) -> None:
        if not self.path.is_file():
            return
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
            if isinstance(raw, dict):
                self.data.update(raw)
        except Exception:
            pass

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.data, indent=2), encoding="utf-8")

    def ingest_neutral(
        self,
        yaw: float,
        pitch: float,
        roll: float,
        face_width: float | None,
        ear: float | None = None,
    ) -> None:
        n = int(self.data.get("samples") or 0)
        w = 1.0 / (n + 1)
        self.data["yaw0"] = (1 - w) * float(self.data.get("yaw0") or 0) + w * yaw
        self.data["pitch0"] = (1 - w) * float(self.data.get("pitch0") or 0) + w * pitch
        self.data["roll0"] = (1 - w) * float(self.data.get("roll0") or 0) + w * roll
        _ema_field(self.data, "face_width0", face_width, w)
        # Only ingest open-eye EAR (skip mid-blink samples).
        if ear is not None and ear > 0.16:
            _ema_field(self.data, "ear0", ear, w)
        self.data["samples"] = n + 1
        if n + 1 >= 20:
            self.save()

    def ingest_posture(
        self,
        mode: str,
        nose_y: float,
        shoulder_y: float | None,
        face_width: float | None,
        ipd: float | None,
    ) -> None:
        """Accumulate sit or stand upper-body baseline (normalized Y in 0..1)."""
        prefix = "sit" if mode == "sit" else "stand"
        n_key = f"{prefix}_samples"
        n = int(self.data.get(n_key) or 0)
        w = 1.0 / (n + 1)
        _ema_field(self.data, f"{prefix}_nose_y", nose_y, w)
        _ema_field(self.data, f"{prefix}_shoulder_y", shoulder_y, w)
        _ema_field(self.data, f"{prefix}_face_width", face_width, w)
        _ema_field(self.data, f"{prefix}_ipd", ipd, w)
        self.data[n_key] = n + 1
        if n + 1 >= 8:
            self.save()

    def sit_stand_ready(self) -> bool:
        return (
            int(self.data.get("sit_samples") or 0) >= 6
            and int(self.data.get("stand_samples") or 0) >= 6
            and self.data.get("sit_nose_y") is not None
            and self.data.get("stand_nose_y") is not None
        )

    def as_public(self) -> dict[str, Any]:
        return {
            "samples": int(self.data.get("samples") or 0),
            "yaw0": round(float(self.data.get("yaw0") or 0), 2),
            "pitch0": round(float(self.data.get("pitch0") or 0), 2),
            "roll0": round(float(self.data.get("roll0") or 0), 2),
            "face_width0": self.data.get("face_width0"),
            "ear0": self.data.get("ear0"),
            "ready": int(self.data.get("samples") or 0) >= 15,
            "sit_samples": int(self.data.get("sit_samples") or 0),
            "stand_samples": int(self.data.get("stand_samples") or 0),
            "sit_stand_ready": self.sit_stand_ready(),
            "sit_nose_y": self.data.get("sit_nose_y"),
            "stand_nose_y": self.data.get("stand_nose_y"),
        }
