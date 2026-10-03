#!/usr/bin/env python3
"""Fast named-pipe gateway client (no PowerShell)."""
from __future__ import annotations

import json
import sys
import uuid
from pathlib import Path

if sys.platform != "win32":
    raise SystemExit("Windows only")

import win32file  # type: ignore
import win32pipe  # type: ignore
import pywintypes  # type: ignore


def gateway(op: str, payload: dict | None = None, timeout_ms: int = 4000) -> dict:
    req = {
        "v": 1,
        "id": str(uuid.uuid4()),
        "op": op,
        "payload": payload or {},
    }
    raw = json.dumps(req, separators=(",", ":")).encode("utf-8")
    handle = None
    deadline = timeout_ms
    for _ in range(12):
        try:
            handle = win32file.CreateFile(
                r"\\.\pipe\calt_enforcer_cmd",
                win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                0,
                None,
                win32file.OPEN_EXISTING,
                0,
                None,
            )
            break
        except pywintypes.error as e:
            # 231 = busy, 2 = not found — wait for a free listener
            if e.winerror not in (231, 2):
                raise
            if not win32pipe.WaitNamedPipe(r"\\.\pipe\calt_enforcer_cmd", min(500, deadline)):
                raise
            deadline = max(100, deadline - 500)
    if handle is None:
        raise RuntimeError("enforcer pipe unavailable")
    try:
        win32pipe.SetNamedPipeHandleState(handle, win32pipe.PIPE_READMODE_MESSAGE, None, None)
        win32file.WriteFile(handle, raw)
        _, data = win32file.ReadFile(handle, 65536)
        return json.loads(data.decode("utf-8"))
    finally:
        win32file.CloseHandle(handle)


if __name__ == "__main__":
    op = sys.argv[1] if len(sys.argv) > 1 else "status.snapshot"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(gateway(op, payload), indent=2))
