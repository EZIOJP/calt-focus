#!/usr/bin/env python3
"""
Phase B — local Focus status sidecar (optional).

Sole status reader for the enforcer pipe when running; FE bus prefers
http://127.0.0.1:18765/snapshot so panels never pile onto the named pipe.

Mirrors under data/productivity/behavior/ are always preferred for chips;
pipe is used at most every ~20s for day.loop_snapshot.

  python scripts/desktop_tracker/run/calt_status_sidecar.py

Stop with Ctrl+C. SoftLand truth remains calt_enforcer — this only caches reads.
"""
from __future__ import annotations

import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PORT = 18765
PIPE_RECONCILE_S = 20.0

REPO = Path(__file__).resolve().parents[3]
BEHAVIOR = REPO / "data" / "productivity" / "behavior"
if not BEHAVIOR.is_dir():
    BEHAVIOR = REPO / "data" / "behavior"

_lock = threading.Lock()
_cache: dict = {
    "updatedAt": 0,
    "softland": None,
    "enforcerStatus": None,
    "dayRollup": None,
    "dayLoop": None,
    "pipeOk": None,
    "source": "idle",
    "ok": True,
}
_last_pipe = 0.0


def _read_json(name: str):
    p = BEHAVIOR / name
    try:
        raw = p.read_text(encoding="utf-8")
        return json.loads(raw)
    except Exception:
        return None


def _pipe_loop() -> dict | None:
    try:
        from _gw_pipe import gateway  # same folder

        r = gateway("day.loop_snapshot", {}, timeout_ms=2500)
        if not isinstance(r, dict) or r.get("error"):
            return None
        loop = r.get("loop")
        if isinstance(loop, dict):
            return loop
        return None
    except Exception:
        return None


def refresh(force_pipe: bool = False) -> dict:
    global _last_pipe
    softland = _read_json("softland_policy.json")
    status = _read_json("enforcer_status.json")
    rollup = _read_json("day_rollup.json")
    day_loop = _cache.get("dayLoop")
    pipe_ok = _cache.get("pipeOk")
    source = "mirrors"
    now = time.time()
    need_pipe = force_pipe or (now - _last_pipe >= PIPE_RECONCILE_S)
    if need_pipe:
        _last_pipe = now
        loop = _pipe_loop()
        if loop is not None:
            day_loop = loop
            pipe_ok = True
            source = "mirrors+pipe"
        else:
            pipe_ok = False if force_pipe else pipe_ok
    snap = {
        "updatedAt": int(now * 1000),
        "softland": softland,
        "enforcerStatus": status,
        "dayRollup": rollup,
        "dayLoop": day_loop,
        "pipeOk": pipe_ok,
        "source": "sidecar",
        "ok": True,
        "detail_source": source,
    }
    with _lock:
        _cache.clear()
        _cache.update(snap)
    return snap


def _bg_loop():
    while True:
        try:
            refresh(False)
        except Exception:
            pass
        time.sleep(3.0)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:  # quiet
        return

    def do_GET(self):  # noqa: N802
        if self.path.split("?")[0] not in ("/snapshot", "/status", "/"):
            self.send_response(404)
            self.end_headers()
            return
        with _lock:
            body = json.dumps(_cache, separators=(",", ":")).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):  # noqa: N802
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.end_headers()


def main() -> None:
    os.chdir(Path(__file__).resolve().parent)
    refresh(True)
    threading.Thread(target=_bg_loop, daemon=True).start()
    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"calt_status_sidecar listening on http://127.0.0.1:{PORT}/snapshot", flush=True)
    print(f"behavior dir: {BEHAVIOR}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped", flush=True)


if __name__ == "__main__":
    main()
