"""MediaPipe Face Mesh + Pose → gaze/posture metrics (ErgoGuard / PosturePro style).

Sit/stand uses upper-body Y-shift + face scale vs dual calibration (laptop cams
rarely see hips). Frames stay in RAM only — landmarks written, frame discarded.
"""
from __future__ import annotations

import math
import time
from typing import Any

import cv2
import numpy as np

from calibration import CalibrationStore, Ema


def _angle_deg(a: np.ndarray, b: np.ndarray, c: np.ndarray) -> float:
    ba = a - b
    bc = c - b
    na = np.linalg.norm(ba)
    nc = np.linalg.norm(bc)
    if na < 1e-6 or nc < 1e-6:
        return 0.0
    cos = float(np.clip(np.dot(ba, bc) / (na * nc), -1.0, 1.0))
    return float(math.degrees(math.acos(cos)))


def _ear(eye: list[np.ndarray]) -> float:
    """Eye aspect ratio from 6 points (MediaPipe iris-ish corners)."""
    if len(eye) < 6:
        return 0.3
    v1 = np.linalg.norm(eye[1] - eye[5])
    v2 = np.linalg.norm(eye[2] - eye[4])
    h = np.linalg.norm(eye[0] - eye[3])
    if h < 1e-6:
        return 0.3
    return float((v1 + v2) / (2.0 * h))


class BlinkRateTracker:
    """Rising-edge blink counter → blinks per minute (rolling ~60s)."""

    def __init__(self) -> None:
        self.events = 0
        self.was_closed = False
        self.window_start = time.time()
        self.last_blink_at: float | None = None

    def update(self, ear: float, ear_baseline: float | None) -> float:
        # Prefer calibrated open-eye EAR; else a forgiving absolute floor.
        base = float(ear_baseline) if ear_baseline and ear_baseline > 0.05 else 0.28
        closed_thr = max(0.10, base * 0.55)
        open_thr = max(0.14, base * 0.78)

        if ear < closed_thr and not self.was_closed:
            self.events += 1
            self.was_closed = True
            self.last_blink_at = time.time()
        elif ear > open_thr:
            self.was_closed = False

        now = time.time()
        elapsed = max(1.0, now - self.window_start)
        rate = (self.events / elapsed) * 60.0
        if elapsed >= 60.0:
            self.events = 0
            self.window_start = now
        return float(rate)

    def reset(self) -> None:
        self.events = 0
        self.was_closed = False
        self.window_start = time.time()
        self.last_blink_at = None


class VisionEngine:
    def __init__(self, cal: CalibrationStore) -> None:
        self.cal = cal
        self._mp = None
        self._face = None
        self._pose = None
        self._yaw_e = Ema(0.4)
        self._pitch_e = Ema(0.4)
        self._roll_e = Ema(0.35)
        self._attn_e = Ema(0.3)
        self._post_e = Ema(0.3)
        self._stand_score_e = Ema(0.35)
        self._blink = BlinkRateTracker()
        self._ear_e = Ema(0.4)
        self._last_hint = "unknown"
        self._init_mediapipe()

    def _init_mediapipe(self) -> None:
        import mediapipe as mp

        self._mp = mp
        self._face = mp.solutions.face_mesh.FaceMesh(
            static_image_mode=False,
            max_num_faces=1,
            refine_landmarks=True,
            min_detection_confidence=0.5,
            min_tracking_confidence=0.5,
        )
        self._pose = mp.solutions.pose.Pose(
            static_image_mode=False,
            model_complexity=1,
            enable_segmentation=False,
            min_detection_confidence=0.5,
            min_tracking_confidence=0.5,
        )

    def close(self) -> None:
        if self._face:
            self._face.close()
        if self._pose:
            self._pose.close()

    def process(
        self,
        frame_bgr: np.ndarray,
        calibrate: bool = False,
        calibrate_posture: str | None = None,
    ) -> dict[str, Any]:
        h, w = frame_bgr.shape[:2]
        rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        face_res = self._face.process(rgb) if self._face else None
        pose_res = self._pose.process(rgb) if self._pose else None

        sample: dict[str, Any] = {
            "present": False,
            "attention_pct": 0.0,
            "gaze_strength": 0.0,
            "blink_rate": 0.0,
            "ear": None,
            "blink_just_now": False,
            "yaw": 0.0,
            "pitch": 0.0,
            "roll": 0.0,
            "neck_angle": None,
            "torso_angle": None,
            "posture_score": 50.0,
            "distance_proxy": None,
            "sit_stand_hint": "unknown",
            "sit_stand_score": None,
            "sit_stand_ready": self.cal.sit_stand_ready(),
            "face_width": None,
            "nose_y": None,
            "shoulder_y": None,
            "ipd": None,
        }

        if not face_res or not face_res.multi_face_landmarks:
            return sample

        lm = face_res.multi_face_landmarks[0].landmark
        sample["present"] = True

        def pt(i: int) -> np.ndarray:
            return np.array([lm[i].x * w, lm[i].y * h, lm[i].z * w], dtype=np.float64)

        # Head pose approx from face geometry (nose / cheeks / chin).
        nose = pt(1)
        chin = pt(152)
        left_cheek = pt(234)
        right_cheek = pt(454)
        forehead = pt(10)

        mid_eyes = (pt(33) + pt(263)) * 0.5
        face_width = float(np.linalg.norm(left_cheek[:2] - right_cheek[:2]))
        ipd = float(np.linalg.norm(pt(33)[:2] - pt(263)[:2]))
        sample["face_width"] = round(face_width, 2)
        sample["ipd"] = round(ipd, 2)
        nose_y_n = float(nose[1] / max(float(h), 1.0))
        sample["nose_y"] = round(nose_y_n, 4)

        # Yaw from cheek asymmetry; pitch from nose vs eye mid; roll from eye line.
        cheek_dx = float(right_cheek[0] - left_cheek[0])
        yaw = float(np.clip((nose[0] - (left_cheek[0] + right_cheek[0]) * 0.5) / max(cheek_dx, 1.0) * 90.0, -60, 60))
        pitch = float(np.clip((nose[1] - mid_eyes[1]) / max(abs(chin[1] - forehead[1]), 1.0) * 90.0, -45, 45))
        eye_vec = pt(263)[:2] - pt(33)[:2]
        roll = float(np.clip(math.degrees(math.atan2(eye_vec[1], eye_vec[0])), -40, 40))

        yaw_s = self._yaw_e.update(yaw) or yaw
        pitch_s = self._pitch_e.update(pitch) or pitch
        roll_s = self._roll_e.update(roll) or roll
        sample["yaw"] = round(yaw_s, 2)
        sample["pitch"] = round(pitch_s, 2)
        sample["roll"] = round(roll_s, 2)

        # Iris / EAR blink → blinks per minute
        left_eye = [pt(i) for i in (33, 160, 158, 133, 153, 144)]
        right_eye = [pt(i) for i in (362, 385, 387, 263, 373, 380)]
        ear_raw = (_ear(left_eye) + _ear(right_eye)) * 0.5
        ear = self._ear_e.update(ear_raw) or ear_raw
        sample["ear"] = round(float(ear), 3)

        if calibrate:
            self.cal.ingest_neutral(yaw_s, pitch_s, roll_s, face_width, ear=float(ear))

        before_blinks = self._blink.events
        blink_rate = self._blink.update(float(ear), self.cal.data.get("ear0"))
        sample["blink_rate"] = round(blink_rate, 1)
        sample["blink_just_now"] = self._blink.events > before_blinks
        # Soften attention when eyes closed mid-blink
        if self._blink.was_closed:
            ear_for_attn = 0.12
        else:
            ear_for_attn = float(ear)

        # Gaze strength: iris relative to eye corners (simple)
        left_iris = pt(468) if len(lm) > 468 else (left_eye[0] + left_eye[3]) * 0.5
        right_iris = pt(473) if len(lm) > 473 else (right_eye[0] + right_eye[3]) * 0.5

        def gaze_one(iris: np.ndarray, corners: list[np.ndarray]) -> float:
            mid = (corners[0] + corners[3]) * 0.5
            span = max(float(np.linalg.norm(corners[0][:2] - corners[3][:2])), 1.0)
            return float(1.0 - min(1.0, np.linalg.norm(iris[:2] - mid[:2]) / (0.35 * span)))

        gaze = (gaze_one(left_iris, left_eye) + gaze_one(right_iris, right_eye)) * 0.5
        sample["gaze_strength"] = round(gaze, 3)

        yaw0 = float(self.cal.data.get("yaw0") or 0)
        pitch0 = float(self.cal.data.get("pitch0") or 0)
        dy = abs(yaw_s - yaw0)
        dp = abs(pitch_s - pitch0)
        attn = max(0.0, 100.0 - dy * 2.2 - dp * 2.5) * (0.55 + 0.45 * gaze)
        if ear_for_attn < 0.16:
            attn *= 0.4
        attn_s = self._attn_e.update(attn) or attn
        sample["attention_pct"] = round(float(np.clip(attn_s, 0, 100)), 1)

        # Distance proxy: calibrated face width / current (larger face → closer → higher proxy)
        fw0 = self.cal.data.get("face_width0")
        if fw0 and face_width > 0:
            sample["distance_proxy"] = round(float(fw0) / float(face_width), 3)
        else:
            sample["distance_proxy"] = round(float(face_width) / max(float(w), 1.0), 3)

        # Pose: neck + torso + optional shoulder Y / hip fallback
        neck_angle = None
        torso_angle = None
        shoulder_y_n: float | None = None
        hip_ratio_hint: str | None = None
        if pose_res and pose_res.pose_landmarks:
            pl = pose_res.pose_landmarks.landmark
            mp_pose = self._mp.solutions.pose.PoseLandmark

            def ppt(idx) -> np.ndarray:
                p = pl[idx]
                return np.array([p.x * w, p.y * h, p.z * w], dtype=np.float64)

            l_sh = ppt(mp_pose.LEFT_SHOULDER)
            r_sh = ppt(mp_pose.RIGHT_SHOULDER)
            l_hip = ppt(mp_pose.LEFT_HIP)
            r_hip = ppt(mp_pose.RIGHT_HIP)
            nose_p = ppt(mp_pose.NOSE)
            mid_sh = (l_sh + r_sh) * 0.5
            mid_hip = (l_hip + r_hip) * 0.5
            shoulder_y_n = float(mid_sh[1] / max(float(h), 1.0))
            sample["shoulder_y"] = round(shoulder_y_n, 4)

            neck_angle = _angle_deg(nose_p, mid_sh, mid_hip)
            torso_vec = mid_sh[:2] - mid_hip[:2]
            torso_angle = float(abs(90.0 - abs(math.degrees(math.atan2(torso_vec[1], torso_vec[0])))))
            sample["neck_angle"] = round(neck_angle, 1)
            sample["torso_angle"] = round(torso_angle, 1)

            # Hip fallback only when hips look in-frame (visible + below shoulders).
            l_vis = float(getattr(pl[mp_pose.LEFT_HIP], "visibility", 0) or 0)
            r_vis = float(getattr(pl[mp_pose.RIGHT_HIP], "visibility", 0) or 0)
            sh_w = float(np.linalg.norm(l_sh[:2] - r_sh[:2]))
            torso_h = float(abs(mid_sh[1] - mid_hip[1]))
            if l_vis > 0.55 and r_vis > 0.55 and sh_w > 1 and mid_hip[1] > mid_sh[1]:
                ratio = torso_h / sh_w
                hip_ratio_hint = "sit" if ratio < 1.35 else "stand"

        if calibrate_posture in ("sit", "stand"):
            self.cal.ingest_posture(
                calibrate_posture,
                nose_y=nose_y_n,
                shoulder_y=shoulder_y_n,
                face_width=face_width,
                ipd=ipd,
            )

        sit_stand, stand_score = self._classify_sit_stand(
            nose_y_n=nose_y_n,
            shoulder_y_n=shoulder_y_n,
            face_width=face_width,
            ipd=ipd,
            hip_hint=hip_ratio_hint,
        )
        sample["sit_stand_hint"] = sit_stand
        sample["sit_stand_score"] = round(stand_score, 3) if stand_score is not None else None
        sample["sit_stand_ready"] = self.cal.sit_stand_ready()

        # Posture score: high is good. Penalize chin-down (pitch) + forward neck.
        pitch_pen = max(0.0, (pitch_s - pitch0) - 6.0) * 3.0
        neck_pen = 0.0
        if neck_angle is not None:
            neck_pen = max(0.0, 165.0 - neck_angle) * 1.5
        torso_pen = 0.0
        if torso_angle is not None:
            torso_pen = max(0.0, torso_angle - 12.0) * 1.2
        post = max(0.0, 100.0 - pitch_pen - neck_pen - torso_pen)
        post_s = self._post_e.update(post) or post
        sample["posture_score"] = round(float(np.clip(post_s, 0, 100)), 1)
        return sample

    def _classify_sit_stand(
        self,
        nose_y_n: float,
        shoulder_y_n: float | None,
        face_width: float,
        ipd: float,
        hip_hint: str | None,
    ) -> tuple[str, float | None]:
        """Upper-body sit/stand from calibrated Y + face scale; hip only as tie-break.

        Standing typically moves nose/shoulders upward in frame (smaller Y) and/or
        shrinks face/IPD when the user steps back. Dual calibration is required for
        a confident label; otherwise return unknown (or weak hip fallback).
        """
        d = self.cal.data
        sit_n = int(d.get("sit_samples") or 0)
        stand_n = int(d.get("stand_samples") or 0)
        y_sit = d.get("sit_nose_y")
        y_stand = d.get("stand_nose_y")

        if sit_n >= 6 and stand_n >= 6 and y_sit is not None and y_stand is not None:
            dist_sit = abs(nose_y_n - float(y_sit)) * 1.0
            dist_stand = abs(nose_y_n - float(y_stand)) * 1.0

            sh_sit = d.get("sit_shoulder_y")
            sh_stand = d.get("stand_shoulder_y")
            if shoulder_y_n is not None and sh_sit is not None and sh_stand is not None:
                dist_sit += abs(shoulder_y_n - float(sh_sit)) * 0.85
                dist_stand += abs(shoulder_y_n - float(sh_stand)) * 0.85

            fw_sit = d.get("sit_face_width")
            fw_stand = d.get("stand_face_width")
            if face_width > 0 and fw_sit and fw_stand:
                # Normalize face-width delta by sit baseline so units match Y space.
                scale = max(float(fw_sit), 1.0)
                dist_sit += abs(face_width - float(fw_sit)) / scale * 0.55
                dist_stand += abs(face_width - float(fw_stand)) / scale * 0.55

            ipd_sit = d.get("sit_ipd")
            ipd_stand = d.get("stand_ipd")
            if ipd > 0 and ipd_sit and ipd_stand:
                scale_i = max(float(ipd_sit), 1.0)
                dist_sit += abs(ipd - float(ipd_sit)) / scale_i * 0.35
                dist_stand += abs(ipd - float(ipd_stand)) / scale_i * 0.35

            # Hip ratio: small weight only when visible.
            if hip_hint == "sit":
                dist_sit *= 0.9
            elif hip_hint == "stand":
                dist_stand *= 0.9

            total = dist_sit + dist_stand
            # stand_score 0=sit .. 1=stand
            raw = float(dist_sit / total) if total > 1e-6 else 0.5
            stand_score = self._stand_score_e.update(raw) or raw

            # Hysteresis: stick to last label near the middle.
            if self._last_hint == "stand":
                hint = "stand" if stand_score >= 0.42 else "sit"
            elif self._last_hint == "sit":
                hint = "sit" if stand_score <= 0.58 else "stand"
            else:
                hint = "stand" if stand_score >= 0.5 else "sit"
            self._last_hint = hint
            return hint, float(stand_score)

        # Sit-only: treat a clear upward jump (ΔY) or face shrink as stand.
        if sit_n >= 6 and y_sit is not None:
            dy = float(y_sit) - nose_y_n  # positive → nose higher in frame
            fw_sit = d.get("sit_face_width")
            shrink = 0.0
            if fw_sit and face_width > 0:
                shrink = (float(fw_sit) - face_width) / max(float(fw_sit), 1.0)
            score = float(np.clip(dy / 0.08 + shrink * 0.8, 0.0, 1.0))
            stand_score = self._stand_score_e.update(score) or score
            if stand_score >= 0.55:
                self._last_hint = "stand"
                return "stand", float(stand_score)
            if stand_score <= 0.35:
                self._last_hint = "sit"
                return "sit", float(stand_score)
            return self._last_hint if self._last_hint != "unknown" else "sit", float(stand_score)

        if hip_hint in ("sit", "stand"):
            self._last_hint = hip_hint
            return hip_hint, None

        return "unknown", None
