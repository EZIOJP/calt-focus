"""Localhost HTTP control for in-app recalibration (Focus WebView → sidecar).

  GET  http://127.0.0.1:8766/health
  POST http://127.0.0.1:8766/calibrate  {"mode":"neutral"|"sit"|"stand"}

Bind is 127.0.0.1 only. SoftLand/Arm untouched.
"""
from __future__ import annotations

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Callable
from urllib.parse import urlparse


DEFAULT_PORT = 8766

DURATIONS = {
    "neutral": 12.0,
    "sit": 4.0,
    "stand": 4.0,
}


class ControlState:
    """Thread-safe calibrate request + status for the vision loop."""

    def __init__(self, port: int = DEFAULT_PORT) -> None:
        self.port = port
        self._lock = threading.Lock()
        self.mode: str | None = None  # neutral | sit | stand
        self.until = 0.0
        self.last_error: str | None = None
        self.last_ok: str | None = None
        self.started = False
        self._cal_public: Callable[[], dict[str, Any]] | None = None

    def bind_calibration(self, fn: Callable[[], dict[str, Any]]) -> None:
        self._cal_public = fn

    def request(self, mode: str) -> tuple[bool, str]:
        mode = (mode or "").strip().lower()
        if mode not in DURATIONS:
            return False, "bad_mode"
        with self._lock:
            if self.mode and time.time() < self.until:
                return False, f"busy:{self.mode}"
            self.mode = mode
            self.until = time.time() + DURATIONS[mode]
            self.last_error = None
            self.last_ok = None
        return True, f"started:{mode}"

    def active(self) -> tuple[bool, str | None]:
        """Return (calibrate_neutral, posture_mode) for this frame (no clear).

        Stays active until finish_if_due() clears, so the last frames still ingest.
        """
        with self._lock:
            if not self.mode:
                return False, None
            if self.mode == "neutral":
                return True, None
            return False, self.mode

    def finish_if_due(self) -> str | None:
        """If a session just ended, clear it and return mode name (caller saves cal)."""
        with self._lock:
            now = time.time()
            if self.mode and now >= self.until:
                done = self.mode
                self.last_ok = done
                self.mode = None
                self.until = 0.0
                return done
            return None

    def as_public(self) -> dict[str, Any]:
        with self._lock:
            now = time.time()
            active = self.mode
            remain = max(0.0, self.until - now) if active else 0.0
            cal = self._cal_public() if self._cal_public else {}
            return {
                "port": self.port,
                "started": self.started,
                "calibrating": active,
                "remain_sec": round(remain, 1),
                "last_ok": self.last_ok,
                "last_error": self.last_error,
                "sit_stand_ready": bool(cal.get("sit_stand_ready")),
                "sit_samples": cal.get("sit_samples"),
                "stand_samples": cal.get("stand_samples"),
                "gaze_ready": bool(cal.get("ready")),
                "gaze_samples": cal.get("samples"),
            }


def start_control_server(state: ControlState) -> ThreadingHTTPServer | None:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args: Any) -> None:  # noqa: A003
            return

        def _cors(self) -> None:
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")

        def _json(self, code: int, payload: dict[str, Any]) -> None:
            raw = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self._cors()
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_OPTIONS(self) -> None:  # noqa: N802
            self.send_response(204)
            self._cors()
            self.end_headers()

        def do_GET(self) -> None:  # noqa: N802
            path = urlparse(self.path).path
            if path in ("/", "/health", "/status"):
                self._json(200, {"ok": True, **state.as_public()})
                return
            self._json(404, {"ok": False, "error": "not_found"})

        def do_POST(self) -> None:  # noqa: N802
            path = urlparse(self.path).path
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(max(0, min(length, 4096))) if length else b"{}"
            try:
                data = json.loads(body.decode("utf-8") or "{}")
            except Exception:
                data = {}
            if path == "/calibrate":
                mode = str(data.get("mode") or "")
                ok, detail = state.request(mode)
                self._json(200 if ok else 409, {"ok": ok, "detail": detail, **state.as_public()})
                return
            self._json(404, {"ok": False, "error": "not_found"})

    try:
        server = ThreadingHTTPServer(("127.0.0.1", state.port), Handler)
    except OSError as e:
        state.last_error = f"bind_failed:{e}"
        print(f"bodycare control: cannot bind :{state.port} — {e}", flush=True)
        return None

    state.started = True
    t = threading.Thread(target=server.serve_forever, name="bodycare-control", daemon=True)
    t.start()
    print(f"bodycare control http://127.0.0.1:{state.port}/  (POST /calibrate)", flush=True)
    return server
