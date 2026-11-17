"""
CALT Focus body-care sidecar — MediaPipe gaze + pose → behavior mirrors.

  python main.py                    # live webcam, write body_care.json
  python main.py --calibrate        # hold neutral gaze ~12s
  python main.py --calibrate-sit    # sit baseline ~4s (upper body in frame)
  python main.py --calibrate-stand  # stand baseline ~4s
  python main.py --camera 1         # alternate camera index
  python main.py --control-port 8766

In-app recalibrate: POST http://127.0.0.1:8766/calibrate {"mode":"neutral"|"sit"|"stand"}

Frames stay in RAM only; only landmarks + posture state are mirrored.
Does not touch SoftLand / Arm. Prefs + nudges stay in Focus WebView.
"""
from __future__ import annotations

import argparse
import sys
import time
from datetime import datetime
from pathlib import Path

import cv2

from calibration import CalibrationStore
from control import ControlState, start_control_server
from mirrors import MirrorWriter, repo_behavior_dir
from vision import VisionEngine


def main() -> int:
    ap = argparse.ArgumentParser(description="CALT Focus body-care vision sidecar")
    ap.add_argument("--camera", type=int, default=0)
    ap.add_argument("--hz", type=float, default=2.0, help="Write rate (samples/sec)")
    ap.add_argument("--calibrate", action="store_true", help="Ingest neutral pose for ~12s")
    ap.add_argument(
        "--calibrate-sit",
        action="store_true",
        help="Record sitting upper-body baseline (~4s)",
    )
    ap.add_argument(
        "--calibrate-stand",
        action="store_true",
        help="Record standing upper-body baseline (~4s)",
    )
    ap.add_argument("--preview", action="store_true", help="Show OpenCV preview window")
    ap.add_argument("--behavior-dir", type=Path, default=None)
    ap.add_argument(
        "--control-port",
        type=int,
        default=8766,
        help="Localhost HTTP control port (0=off)",
    )
    ap.add_argument(
        "--oneshot-exit",
        action="store_true",
        help="Exit after CLI sit/stand calibrate (bat scripts)",
    )
    args = ap.parse_args()

    if args.calibrate_sit and args.calibrate_stand:
        print("ERROR: use only one of --calibrate-sit / --calibrate-stand", file=sys.stderr)
        return 2

    behavior = args.behavior_dir or repo_behavior_dir()
    cal_path = behavior / "body_care_calibration.json"
    cal = CalibrationStore(cal_path)
    writer = MirrorWriter(behavior)
    engine = VisionEngine(cal)
    ctrl = ControlState(port=max(0, int(args.control_port)))
    ctrl.bind_calibration(cal.as_public)
    server = None
    if ctrl.port > 0:
        server = start_control_server(ctrl)

    # Seed CLI calibrate into the same control state (keeps one code path).
    oneshot = bool(args.oneshot_exit or args.calibrate_sit or args.calibrate_stand)
    if args.calibrate:
        ctrl.request("neutral")
    if args.calibrate_sit:
        ctrl.request("sit")
    if args.calibrate_stand:
        ctrl.request("stand")

    cap = cv2.VideoCapture(args.camera, cv2.CAP_DSHOW)
    if not cap.isOpened():
        cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        print(f"ERROR: cannot open camera {args.camera}", file=sys.stderr)
        return 1

    interval = 1.0 / max(0.5, min(10.0, args.hz))
    print(
        f"bodycare_sidecar started · camera={args.camera} · hz={args.hz} · "
        f"behavior={behavior} · control=:{ctrl.port if server else 'off'}",
        flush=True,
    )

    last_write = 0.0
    try:
        while True:
            ok, frame = cap.read()
            if not ok or frame is None:
                time.sleep(0.05)
                continue
            now = time.time()
            calibrating, active_posture = ctrl.active()
            sample = engine.process(
                frame,
                calibrate=calibrating,
                calibrate_posture=active_posture,
            )
            sample["ts"] = datetime.now().isoformat(timespec="seconds")
            sample["calibrating"] = ctrl.as_public().get("calibrating")

            done = ctrl.finish_if_due()
            if done:
                cal.save()
                print(f"Calibration ({done}) saved → {cal_path}", flush=True)
                # Bat one-shot: exit after CLI-triggered session completes.
                if oneshot and (
                    (args.calibrate and done == "neutral")
                    or (args.calibrate_sit and done == "sit")
                    or (args.calibrate_stand and done == "stand")
                ):
                    del frame
                    break

            if now - last_write >= interval:
                last_write = now
                pub = cal.as_public()
                writer.write_sample(
                    sample,
                    calibration=pub,
                    control=ctrl.as_public(),
                )
                st = ctrl.as_public()
                if st.get("calibrating"):
                    print(
                        f"calibrating {st['calibrating']}… remain={st.get('remain_sec')}s "
                        f"sit_n={pub.get('sit_samples')} stand_n={pub.get('stand_samples')} "
                        f"gaze_n={pub.get('samples')} blink={sample.get('blink_rate')}/m",
                        flush=True,
                    )
                else:
                    blink_flag = " *" if sample.get("blink_just_now") else ""
                    print(
                        f"{sample['ts']} present={sample.get('present')} "
                        f"attn={sample.get('attention_pct')} posture={sample.get('posture_score')} "
                        f"blink={sample.get('blink_rate')}/m{blink_flag} "
                        f"dist={sample.get('distance_proxy')} sit={sample.get('sit_stand_hint')} "
                        f"score={sample.get('sit_stand_score')}",
                        flush=True,
                    )

            if args.preview:
                label = (
                    f"a={sample.get('attention_pct')} p={sample.get('posture_score')} "
                    f"sit={sample.get('sit_stand_hint')} cal={sample.get('calibrating') or '-'}"
                )
                cv2.putText(frame, label, (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (40, 220, 120), 2)
                cv2.imshow("CALT bodycare", frame)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    del frame
                    break
            else:
                time.sleep(0.01)

            del frame
    except KeyboardInterrupt:
        print("stopped", flush=True)
    finally:
        if server is not None:
            try:
                server.shutdown()
            except Exception:
                pass
        engine.close()
        cap.release()
        if args.preview:
            cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
