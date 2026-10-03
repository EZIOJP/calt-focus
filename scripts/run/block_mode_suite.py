#!/usr/bin/env python3
"""
CALT Focus block-mode test suite — SoftLand (sites) + Arm (apps) + sync.

Modes:
  --softland-only   Fixture SoftLand decide via msg_host + CALT_DATA_DIR (safe, default-ish)
  --live            Also probe live policy + gateway + optional Arm fixture kills
  --arm-kills       Live Arm kill tests with disposable fixture EXEs (requires healthy gateway)
  --no-restart      Never restart enforcer if pipe is down
  --json PATH       Write machine-readable results JSON
  --md PATH         Write markdown report (default under docs/superpowers/exports/)

Always snapshots SoftLand/Arm mirrors before live mutations and restores in finally.

Product locks:
  SoftLand ON != Arm. SoftLand = sites; Arm = process kills.
  SoftLand decide: Gate -> msg_host -> get_mode (not Study :8000).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


# ---------------------------------------------------------------------------
# Paths / helpers
# ---------------------------------------------------------------------------


def find_repo() -> Path:
    p = Path(__file__).resolve().parent
    for _ in range(8):
        if (p / "AGENTS.md").exists() and (p / "data").exists():
            return p
        p = p.parent
    raise SystemExit("repo root not found")


REPO = find_repo()
BEH = REPO / "data" / "productivity" / "behavior"
DB = REPO / "data" / "productivity" / "productivity.db"
MSG_HOST = REPO / "calt-focus" / "backend" / "calt_msg_host" / "build" / "calt_msg_host.exe"
GATEWAY_PS1 = REPO / "scripts" / "desktop_tracker" / "run" / "gateway_cmd.ps1"
FIXTURE_DIR = REPO / "scripts" / "desktop_tracker" / "test_fixtures"
BLOCK_TARGET = FIXTURE_DIR / "calt_test_block_target.exe"
SAFE_TARGET = FIXTURE_DIR / "calt_test_safe.exe"
ENFORCER_CANDIDATES = [
    REPO / "calt-focus" / "backend" / "calt_enforcer" / "build" / "Release" / "calt_enforcer.exe",
    REPO / "calt-focus" / "backend" / "calt_enforcer" / "build" / "calt_enforcer.exe",
    REPO / "scripts" / "desktop_tracker" / "installer" / "installer_payload" / "bin" / "calt_enforcer.exe",
]


@dataclass
class CaseResult:
    id: str
    layer: str  # softland_fixture | softland_live | arm_unit | arm_live | sync | gateway
    ok: bool
    expected: Any
    actual: Any
    detail: str = ""
    skipped: bool = False


@dataclass
class SuiteReport:
    started_at: str
    finished_at: str = ""
    results: list[CaseResult] = field(default_factory=list)
    restored: bool = False
    gateway_ok: bool = False
    notes: list[str] = field(default_factory=list)

    @property
    def passed(self) -> int:
        return sum(1 for r in self.results if r.ok and not r.skipped)

    @property
    def failed(self) -> int:
        return sum(1 for r in self.results if not r.ok and not r.skipped)

    @property
    def skipped(self) -> int:
        return sum(1 for r in self.results if r.skipped)


def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def add(rep: SuiteReport, **kwargs: Any) -> CaseResult:
    c = CaseResult(**kwargs)
    rep.results.append(c)
    return c


# ---------------------------------------------------------------------------
# SoftLand fixture decide (msg_host + CALT_DATA_DIR)
# ---------------------------------------------------------------------------


def msg_host_get_mode(url: str, data_dir: Path, exe: Path = MSG_HOST) -> dict:
    if not exe.exists():
        raise FileNotFoundError(f"calt_msg_host.exe missing: {exe}")
    env = os.environ.copy()
    env["CALT_DATA_DIR"] = str(data_dir)
    raw = json.dumps({"type": "get_mode", "url": url}).encode("utf-8")
    p = subprocess.Popen(
        [str(exe)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    assert p.stdin and p.stdout
    p.stdin.write(struct.pack("<I", len(raw)) + raw)
    p.stdin.close()
    hdr = p.stdout.read(4)
    if len(hdr) < 4:
        err = (p.stderr.read() or b"").decode("utf-8", errors="replace")
        raise RuntimeError(f"msg_host no header: {err}")
    n = struct.unpack("<I", hdr)[0]
    body = p.stdout.read(n)
    return json.loads(body.decode("utf-8"))


def live_msg_host_get_mode(url: str) -> dict:
    """Probe against real data/productivity/behavior (no CALT_DATA_DIR)."""
    if not MSG_HOST.exists():
        raise FileNotFoundError(str(MSG_HOST))
    env = os.environ.copy()
    env.pop("CALT_DATA_DIR", None)
    env["CALT_ROOT"] = str(REPO)
    raw = json.dumps({"type": "get_mode", "url": url}).encode("utf-8")
    p = subprocess.Popen(
        [str(MSG_HOST)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        cwd=str(REPO),
    )
    assert p.stdin and p.stdout
    p.stdin.write(struct.pack("<I", len(raw)) + raw)
    p.stdin.close()
    hdr = p.stdout.read(4)
    if len(hdr) < 4:
        raise RuntimeError("msg_host live: empty reply")
    n = struct.unpack("<I", hdr)[0]
    return json.loads(p.stdout.read(n).decode("utf-8"))


def base_fixture_doc() -> dict:
    """Minimal SoftLand policy for matrix cells."""
    return {
        "softland_enabled": True,
        "schema_version": 1,
        "site_rules": {
            "allow_extra": ["meet.google.com", "wikipedia.org", "study.local.test"],
            "watch_extra": ["youtube.com", "reddit.com"],
            "block_extra": ["twitter.com", "x.com", "alwaysblock.example"],
        },
        "schedules": {
            "enabled": True,
            "windows": [
                {
                    "id": "w1",
                    "label": "AllDayStudy",
                    "days": [0, 1, 2, 3, 4, 5, 6],
                    "start": "00:00",
                    "end": "23:59",
                    "mode": "study",
                }
            ],
        },
        "mode_flags": {
            "study": {
                "block_watch_sites": True,
                "block_porn": True,
                "block_social": True,
                "block_keywords": True,
                "block_other": True,
                "strict_allowlist": True,
            },
            "free": {
                "block_watch_sites": False,
                "block_porn": True,
                "block_social": False,
                "block_keywords": False,
                "block_other": False,
                "strict_allowlist": False,
            },
        },
        "runtime": {
            "free_until": None,
            "free_after_hm": None,
            "incubation_until": None,
            "reward_day_active": False,
            "day_pass": {},
            "earned_ledger_seconds": 0,
        },
        "updated_at": now_iso(),
    }


SOFTLAND_CASES: list[dict[str, Any]] = [
    # --- STUDY ---
    {"cell": "study", "url": "https://youtube.com", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://www.youtube.com/watch?v=dQw4w9WgXcQ", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "http://youtube.com", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://m.youtube.com", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://music.youtube.com", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://youtu.be/abc", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://reddit.com", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://www.reddit.com/r/python", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://facebook.com", "action": "block", "reason": "watch_list"},
    {"cell": "study", "url": "https://meet.google.com", "action": "allow", "reason": "allow_list"},
    {"cell": "study", "url": "https://docs.meet.google.com", "action": "allow", "reason": "allow_list"},
    {"cell": "study", "url": "https://wikipedia.org", "action": "allow", "reason": "allow_list"},
    {"cell": "study", "url": "https://en.wikipedia.org/wiki/Test", "action": "allow", "reason": "allow_list"},
    {"cell": "study", "url": "https://pornhub.com", "action": "block", "reason": "porn"},
    {"cell": "study", "url": "https://www.pornhub.com/video", "action": "block", "reason": "porn"},
    {"cell": "study", "url": "https://example.xxx", "action": "block", "reason": "porn"},
    {"cell": "study", "url": "https://twitter.com", "action": "block", "reason": "block_extra"},
    {"cell": "study", "url": "https://x.com/home", "action": "block", "reason": "block_extra"},
    {"cell": "study", "url": "https://alwaysblock.example/path", "action": "block", "reason": "block_extra"},
    {"cell": "study", "url": "https://github.com", "action": "allow", "reason": "not_listed"},
    {"cell": "study", "url": "https://random-not-listed-9f3a.test", "action": "allow", "reason": "not_listed"},
    {"cell": "study", "url": "https://localhost:3000", "action": "allow", "reason": "allow_list"},
    {"cell": "study", "url": "http://127.0.0.1/admin", "action": "allow", "reason": "allow_list"},
    {"cell": "study", "url": "https://192.168.1.1", "action": "allow", "reason": "not_listed"},
    {"cell": "study", "url": "", "action": "allow", "reason": "no_host"},
    {"cell": "study", "url": "not-a-url", "action": "allow", "reason": "not_listed"},  # host=not-a-url
    {"cell": "study", "url": "https://study.local.test", "action": "allow", "reason": "allow_list"},
    # --- FREE ---
    {"cell": "free", "url": "https://youtube.com", "action": "allow", "reason_in": ["reward_day", "free_window", "free_mode"]},
    {"cell": "free", "url": "https://reddit.com", "action": "allow", "reason_in": ["reward_day", "free_window", "free_mode"]},
    {"cell": "free", "url": "https://facebook.com", "action": "allow", "reason_in": ["reward_day", "free_window", "free_mode"]},
    {"cell": "free", "url": "https://pornhub.com", "action": "block", "reason": "porn"},
    {"cell": "free", "url": "https://twitter.com", "action": "block", "reason": "block_extra"},
    {"cell": "free", "url": "https://x.com", "action": "block", "reason": "block_extra"},
    {"cell": "free", "url": "https://meet.google.com", "action": "allow", "reason": "allow_list"},
    {"cell": "free", "url": "https://github.com", "action": "allow", "reason_in": ["reward_day", "free_window", "free_mode"]},
    # --- SoftLand OFF ---
    {"cell": "off", "url": "https://youtube.com", "action": "allow", "reason": "softland_off"},
    {"cell": "off", "url": "https://pornhub.com", "action": "allow", "reason": "softland_off"},
    {"cell": "off", "url": "https://twitter.com", "action": "allow", "reason": "softland_off"},
    {"cell": "off", "url": "https://alwaysblock.example", "action": "allow", "reason": "softland_off"},
    # --- INCUBATION (overrides free) ---
    {"cell": "incubate", "url": "https://youtube.com", "action": "block", "reason": "incubation"},
    {"cell": "incubate", "url": "https://pornhub.com", "action": "block", "reason_in": ["incubation", "porn"]},
    {"cell": "incubate", "url": "https://meet.google.com", "action": "allow", "reason": "allow_list"},
]


def doc_for_cell(cell: str) -> dict:
    doc = json.loads(json.dumps(base_fixture_doc()))
    if cell == "off":
        doc["softland_enabled"] = False
    elif cell == "free":
        doc["runtime"]["free_until"] = "2099-12-31T23:59:59"
        doc["runtime"]["reward_day_active"] = True
    elif cell == "incubate":
        doc["runtime"]["free_until"] = "2099-12-31T23:59:59"
        doc["runtime"]["reward_day_active"] = True
        doc["runtime"]["incubation_until"] = "2099-12-31T23:59:59"
    # study = defaults
    return doc


def run_softland_fixture_matrix(rep: SuiteReport) -> None:
    if not MSG_HOST.exists():
        add(
            rep,
            id="softland.msg_host_present",
            layer="softland_fixture",
            ok=False,
            expected=True,
            actual=False,
            detail=f"missing {MSG_HOST}",
        )
        return

    tmp = Path(tempfile.mkdtemp(prefix="calt_block_suite_"))
    beh = tmp / "behavior"
    beh.mkdir(parents=True)
    try:
        current_cell = None
        for case in SOFTLAND_CASES:
            cell = case["cell"]
            if cell != current_cell:
                current_cell = cell
                doc = doc_for_cell(cell)
                (beh / "softland_policy.json").write_text(json.dumps(doc), encoding="utf-8")
            url = case["url"]
            try:
                r = msg_host_get_mode(url, tmp)
            except Exception as e:
                add(
                    rep,
                    id=f"softland.{cell}.{url or 'empty'}",
                    layer="softland_fixture",
                    ok=False,
                    expected=case.get("action"),
                    actual=str(e),
                    detail="msg_host error",
                )
                continue
            action_ok = r.get("action") == case["action"]
            reason = r.get("reason")
            if "reason" in case:
                reason_ok = reason == case["reason"]
            else:
                reason_ok = reason in case.get("reason_in", [])
            ok = action_ok and reason_ok
            add(
                rep,
                id=f"softland.{cell}.{url or 'empty'}",
                layer="softland_fixture",
                ok=ok,
                expected={"action": case["action"], "reason": case.get("reason") or case.get("reason_in")},
                actual={"action": r.get("action"), "reason": reason, "mode": r.get("mode"), "enforce": r.get("enforce")},
                detail="" if ok else "misfire",
            )
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------------------
# Arm unit: mirror kill.cpp matching (no live kills)
# ---------------------------------------------------------------------------


def normalize_exe_token(raw: str) -> str:
    name = raw.replace("/", "\\")
    if "\\" in name:
        name = name.rsplit("\\", 1)[-1]
    name = name.strip().strip('"').lower()
    return name


def exe_tokens_match(process_base: str, token: str) -> bool:
    pb = process_base.lower()
    t = token.lower()
    if not pb or not t:
        return False
    if pb == t:
        return True
    if "." not in t and pb == t + ".exe":
        return True
    if "." not in pb and t == pb + ".exe":
        return True
    return False


PROTECTED = {
    "explorer.exe",
    "csrss.exe",
    "winlogon.exe",
    "services.exe",
    "lsass.exe",
    "svchost.exe",
    "smss.exe",
    "fontdrvhost.exe",
    "dwm.exe",
    "calt_enforcer.exe",
    "calt_focus.exe",
    "calt_msg_host.exe",
    "python.exe",
    "pythonw.exe",
}


ARM_UNIT_CASES = [
    ("steam.exe", ["steam.exe"], True),
    ("STEAM.EXE", ["steam.exe"], True),
    ("steam", ["steam.exe"], True),
    ("steam.exe", ["steam"], True),
    ("steam.exe", [r"C:\Games\Steam\steam.exe"], True),  # policy path → basename
    ("notepad.exe", ["steam.exe"], False),
    ("calt_test_block_target.exe", ["calt_test_block_target.exe"], True),
    ("calt_test_safe.exe", ["calt_test_block_target.exe"], False),
    ("explorer.exe", ["explorer.exe"], False),  # protected even if listed
    ("python.exe", ["python.exe"], False),
    ("calt_enforcer.exe", ["calt_enforcer.exe"], False),
]


def would_kill(process_exe: str, policy_exes: list[str]) -> bool:
    base = normalize_exe_token(process_exe)
    if base in PROTECTED:
        return False
    tokens = [normalize_exe_token(e) for e in policy_exes if normalize_exe_token(e)]
    return any(exe_tokens_match(base, t) for t in tokens)


def run_arm_unit(rep: SuiteReport) -> None:
    for proc, exes, expect in ARM_UNIT_CASES:
        got = would_kill(proc, exes)
        ok = got == expect
        add(
            rep,
            id=f"arm.unit.{proc}->{exes}",
            layer="arm_unit",
            ok=ok,
            expected=expect,
            actual=got,
            detail="" if ok else "kill-match misfire",
        )


# ---------------------------------------------------------------------------
# Sync / live state diagnostics
# ---------------------------------------------------------------------------


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def sqlite_arm_row() -> dict[str, Any] | None:
    if not DB.exists():
        return None
    con = sqlite3.connect(str(DB))
    try:
        cur = con.cursor()
        row = cur.execute(
            "SELECT hard_block_armed, gate_locked, incubation_active, exes_json "
            "FROM enforcer_runtime ORDER BY id DESC LIMIT 1"
        ).fetchone()
        if not row:
            return None
        return {
            "hard_block_armed": bool(row[0]),
            "gate_locked": bool(row[1]),
            "incubation_active": bool(row[2]),
            "exes": json.loads(row[3] or "[]"),
        }
    finally:
        con.close()


def sqlite_softland_enabled() -> bool | None:
    if not DB.exists():
        return None
    con = sqlite3.connect(str(DB))
    try:
        cur = con.cursor()
        row = cur.execute(
            "SELECT document_json FROM productivity_softland ORDER BY id DESC LIMIT 1"
        ).fetchone()
        if not row or not row[0]:
            return None
        doc = json.loads(row[0])
        return bool(doc.get("softland_enabled"))
    except (sqlite3.Error, json.JSONDecodeError, TypeError):
        return None
    finally:
        con.close()


def effective_arm_from_json(pol: dict) -> dict[str, Any]:
    """Mirror ApplyStickyLock + JSON authority (runtime view)."""
    armed = bool(pol.get("hard_block_armed"))
    locked = bool(pol.get("gate_locked"))
    mode = (pol.get("lock_mode") or "none").lower()
    pwd = pol.get("unlock_password") or ""
    phrase = pol.get("unlock_phrase") or ""
    provided = pol.get("provided_unlock") or ""
    block_disarm = False
    if mode == "password" and pwd and provided != pwd:
        if not armed or not locked:
            block_disarm = True
    elif mode == "phrase" and phrase and provided != phrase:
        if not armed or not locked:
            block_disarm = True
    if block_disarm:
        armed = True
        locked = True
    return {
        "armed": armed,
        "locked": locked,
        "exes": list(pol.get("exes") or []),
        "sticky_forced": block_disarm,
        "lock_mode": mode,
    }


def run_sync_checks(rep: SuiteReport, *, phase: str = "pre") -> None:
    pol_path = BEH / "enforcer_policy.json"
    status_path = BEH / "enforcer_status.json"
    soft_path = BEH / "softland_policy.json"
    prefix = f"sync.{phase}"
    if not pol_path.exists():
        add(rep, id=f"{prefix}.policy_present", layer="sync", ok=False, expected=True, actual=False, detail="missing")
        return

    pol = read_json(pol_path)
    status = read_json(status_path) if status_path.exists() else {}
    soft = read_json(soft_path) if soft_path.exists() else {}
    sql = sqlite_arm_row()
    eff = effective_arm_from_json(pol)

    # Status armed should match effective (sticky) view
    if status:
        ok = bool(status.get("armed")) == bool(eff["armed"])
        add(
            rep,
            id=f"{prefix}.status_vs_effective_arm",
            layer="sync",
            ok=ok,
            expected=eff["armed"],
            actual=status.get("armed"),
            detail="sticky lock may force armed while JSON hard_block_armed is false",
        )

    # SQLite vs JSON: JSON is authoritative for runtime; flag divergence
    if sql is not None:
        json_exes = [normalize_exe_token(x) for x in (pol.get("exes") or [])]
        sql_exes = [normalize_exe_token(x) for x in sql["exes"]]
        exes_match = sorted(json_exes) == sorted(sql_exes)
        armed_match = bool(pol.get("hard_block_armed")) == bool(sql["hard_block_armed"])
        # Divergence is FAIL for observability (UI reading SQLite lies)
        ok = exes_match and armed_match
        add(
            rep,
            id=f"{prefix}.sqlite_vs_json_arm",
            layer="sync",
            ok=ok,
            expected={"json_armed": pol.get("hard_block_armed"), "json_exes": json_exes},
            actual={"sql_armed": sql["hard_block_armed"], "sql_exes": sql_exes},
            detail="" if ok else "SQLite enforcer_runtime stale vs enforcer_policy.json (JSON wins at runtime)",
        )

    soft_en = soft.get("softland_enabled")
    sql_soft = sqlite_softland_enabled()
    if sql_soft is not None and soft_en is not None:
        ok = bool(sql_soft) == bool(soft_en)
        add(
            rep,
            id=f"{prefix}.sqlite_vs_json_softland",
            layer="sync",
            ok=ok,
            expected=soft_en,
            actual=sql_soft,
            detail="" if ok else "SoftLand enabled mismatch SQLite vs mirror",
        )
    else:
        add(
            rep,
            id=f"{prefix}.sqlite_vs_json_softland",
            layer="sync",
            ok=True,
            expected=soft_en,
            actual=sql_soft,
            detail="softland sqlite probe inconclusive — skipped strict compare",
            skipped=sql_soft is None,
        )

    # SoftLand ON != Arm confusion check: report independence
    add(
        rep,
        id=f"{prefix}.softland_ne_arm_independence",
        layer="sync",
        ok=True,
        expected="independent",
        actual={"softland_enabled": soft_en, "json_hard_block_armed": pol.get("hard_block_armed"), "effective_armed": eff["armed"]},
        detail="informational: SoftLand and Arm are separate switches",
    )

    if eff["sticky_forced"] and not pol.get("hard_block_armed"):
        rep.notes.append(
            "ApplyStickyLock: JSON hard_block_armed=false but password/phrase lock forces effective armed=true"
        )
    if eff["armed"] and not eff["exes"]:
        rep.notes.append("Effective Arm ON with empty kill list — no process kills will occur")


# ---------------------------------------------------------------------------
# Gateway + snapshot/restore + live SoftLand + Arm kills
# ---------------------------------------------------------------------------


def gateway_cmd(op: str, payload: dict | None = None, timeout_ms: int = 5000) -> tuple[bool, str]:
    payload = payload or {}
    payload_s = json.dumps(payload, separators=(",", ":"))
    # Write payload to temp file to avoid PowerShell quoting issues
    tmp = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
    try:
        tmp.write(payload_s)
        tmp.close()
        cmd = [
            "powershell",
            "-NoProfile",
            "-File",
            str(GATEWAY_PS1),
            "-Op",
            op,
            "-PayloadFile",
            tmp.name,
            "-TimeoutMs",
            str(timeout_ms),
        ]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=str(REPO))
        out = (r.stdout or "").strip() or (r.stderr or "").strip()
        return r.returncode == 0, out
    finally:
        try:
            os.unlink(tmp.name)
        except OSError:
            pass


def find_enforcer_exe() -> Path | None:
    for p in ENFORCER_CANDIDATES:
        if p.exists():
            return p
    return None


def enforcer_running() -> bool:
    r = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq calt_enforcer.exe"],
        capture_output=True,
        text=True,
    )
    return "calt_enforcer.exe" in (r.stdout or "")


def restart_enforcer_console(rep: SuiteReport) -> bool:
    """Kill wedged console enforcer and relaunch --console. SoftLand stays via msg_host."""
    exe = find_enforcer_exe()
    if not exe:
        rep.notes.append("Cannot restart enforcer: binary not found")
        return False
    subprocess.run(["taskkill", "/F", "/IM", "calt_enforcer.exe"], capture_output=True)
    time.sleep(1.5)
    env = os.environ.copy()
    env["CALT_DB"] = str(DB)
    env["CALT_ENFORCER_LOCK"] = str(BEH / "enforcer_owner.lock")
    subprocess.Popen(
        [str(exe), "--console"],
        cwd=str(REPO),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(2.5)
    ok_alive = enforcer_running()
    ok_gw, out = gateway_cmd("status.snapshot", timeout_ms=6000)
    add(
        rep,
        id="gateway.after_restart",
        layer="gateway",
        ok=ok_gw,
        expected="status.snapshot ok",
        actual=out[:400],
        detail="enforcer alive" if ok_alive else "enforcer not running",
    )
    return ok_gw


@dataclass
class Snapshot:
    softland: dict | None = None
    enforcer_policy: dict | None = None
    unlock_password: str = ""


def take_snapshot() -> Snapshot:
    s = Snapshot()
    soft = BEH / "softland_policy.json"
    pol = BEH / "enforcer_policy.json"
    if soft.exists():
        s.softland = read_json(soft)
    if pol.exists():
        s.enforcer_policy = read_json(pol)
        s.unlock_password = str(s.enforcer_policy.get("unlock_password") or "")
    return s


def restore_snapshot(snap: Snapshot, rep: SuiteReport, via_gateway: bool) -> None:
    """Restore SoftLand + Arm. Prefer gateway; fall back to writing mirrors if pipe dead."""
    errors: list[str] = []
    if via_gateway and snap.enforcer_policy is not None:
        pol = snap.enforcer_policy
        payload = {
            "hard_block_armed": bool(pol.get("hard_block_armed")),
            "gate_locked": bool(pol.get("gate_locked")),
            "incubation_active": bool(pol.get("incubation_active")),
            # Disarmed restore must not re-apply password lock_mode (sticky re-arm).
            "lock_mode": (pol.get("lock_mode") or "none")
            if bool(pol.get("hard_block_armed"))
            else "none",
            "exes": list(pol.get("exes") or []),
            "anti_tamper": bool(pol.get("anti_tamper")),
            "protect_uninstall": bool(pol.get("protect_uninstall")),
        }
        if snap.unlock_password:
            payload["provided_unlock"] = snap.unlock_password
            payload["unlock_password"] = snap.unlock_password
        if pol.get("unlock_phrase"):
            payload["unlock_phrase"] = pol["unlock_phrase"]
            if not snap.unlock_password:
                payload["provided_unlock"] = pol["unlock_phrase"]
        ok, out = gateway_cmd("arm.set", payload, timeout_ms=8000)
        if not ok:
            errors.append(f"arm.restore gateway failed: {out[:200]}")
            # Fall back: write JSON mirror (next enforcer load uses it)
            try:
                (BEH / "enforcer_policy.json").write_text(
                    json.dumps(pol, indent=2) + "\n", encoding="utf-8"
                )
            except OSError as e:
                errors.append(str(e))
    elif snap.enforcer_policy is not None:
        try:
            (BEH / "enforcer_policy.json").write_text(
                json.dumps(snap.enforcer_policy, indent=2) + "\n", encoding="utf-8"
            )
        except OSError as e:
            errors.append(str(e))

    if via_gateway and snap.softland is not None:
        en = bool(snap.softland.get("softland_enabled"))
        ok, out = gateway_cmd("softland.set_enabled", {"enabled": en}, timeout_ms=8000)
        if not ok:
            errors.append(f"softland.restore: {out[:200]}")
            try:
                (BEH / "softland_policy.json").write_text(
                    json.dumps(snap.softland, indent=2) + "\n", encoding="utf-8"
                )
            except OSError as e:
                errors.append(str(e))
    elif snap.softland is not None:
        try:
            (BEH / "softland_policy.json").write_text(
                json.dumps(snap.softland, indent=2) + "\n", encoding="utf-8"
            )
        except OSError as e:
            errors.append(str(e))

    rep.restored = len(errors) == 0
    if errors:
        rep.notes.extend(errors)
        rep.restored = False
        # Still mark attempt
        add(
            rep,
            id="restore.snapshot",
            layer="gateway",
            ok=False,
            expected="restored",
            actual=errors,
        )
    else:
        add(
            rep,
            id="restore.snapshot",
            layer="gateway",
            ok=True,
            expected="restored",
            actual="ok",
            detail="SoftLand/Arm snapshot restored",
        )


def process_alive(image_name: str) -> bool:
    """True if a process with this image name is running.

    tasklist truncates long Image Name columns (e.g. calt_test_block_target.ex),
    so match on the basename stem as well as the full name.
    """
    name = image_name.lower()
    stem = name[:-4] if name.endswith(".exe") else name
    # Prefer PowerShell — full ProcessName without truncation.
    r = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            f"Get-Process -Name '{stem}' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id",
        ],
        capture_output=True,
        text=True,
    )
    if (r.stdout or "").strip():
        return True
    # Fallback tasklist (truncated names OK via stem)
    r2 = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {image_name}"],
        capture_output=True,
        text=True,
    )
    out = (r2.stdout or "").lower()
    return stem in out


def run_live_softland_probes(rep: SuiteReport) -> None:
    soft = read_json(BEH / "softland_policy.json")
    enabled = bool(soft.get("softland_enabled"))
    urls = [
        "https://youtube.com",
        "https://pornhub.com",
        "https://meet.google.com",
        "https://github.com",
        "https://twitter.com",
    ]
    for u in urls:
        try:
            r = live_msg_host_get_mode(u)
        except Exception as e:
            add(rep, id=f"softland.live.{u}", layer="softland_live", ok=False, expected="reply", actual=str(e))
            continue
        if not enabled:
            ok = r.get("action") == "allow" and r.get("reason") == "softland_off"
        else:
            ok = r.get("action") in ("allow", "block")  # structural
        add(
            rep,
            id=f"softland.live.{u}",
            layer="softland_live",
            ok=ok,
            expected="softland_off allow" if not enabled else "valid action",
            actual={"action": r.get("action"), "reason": r.get("reason"), "mode": r.get("mode")},
        )


def run_arm_live_kills(rep: SuiteReport, snap: Snapshot) -> None:
    if not BLOCK_TARGET.exists() or not SAFE_TARGET.exists():
        add(
            rep,
            id="arm.live.fixtures_built",
            layer="arm_live",
            ok=False,
            expected=True,
            actual=False,
            detail="run scripts/desktop_tracker/test_fixtures/build_fixtures.bat",
        )
        return

    pwd = snap.unlock_password
    # Arm with ONLY disposable target — never include real games during the test window
    payload = {
        "hard_block_armed": True,
        "gate_locked": True,
        "lock_mode": (snap.enforcer_policy or {}).get("lock_mode") or "password",
        "exes": ["calt_test_block_target.exe"],
        "anti_tamper": False,
        "protect_uninstall": False,
    }
    if pwd:
        payload["provided_unlock"] = pwd
        payload["unlock_password"] = pwd

    ok, out = gateway_cmd("arm.set", payload, timeout_ms=8000)
    add(
        rep,
        id="arm.live.set_fixture_list",
        layer="arm_live",
        ok=ok,
        expected="arm.set ok",
        actual=out[:300],
    )
    if not ok:
        return

    time.sleep(1.0)

    # Launch safe (must survive) + block target (must die)
    safe_p = subprocess.Popen([str(SAFE_TARGET)], cwd=str(FIXTURE_DIR))
    block_p = subprocess.Popen([str(BLOCK_TARGET)], cwd=str(FIXTURE_DIR))
    time.sleep(3.5)  # allow enforcer tick

    safe_alive = process_alive("calt_test_safe.exe")
    block_alive = process_alive("calt_test_block_target.exe")

    add(
        rep,
        id="arm.live.safe_not_killed",
        layer="arm_live",
        ok=safe_alive,
        expected=True,
        actual=safe_alive,
        detail="misfire if killed",
    )
    add(
        rep,
        id="arm.live.block_target_killed",
        layer="arm_live",
        ok=not block_alive,
        expected=False,
        actual=block_alive,
        detail="expected killed when Arm ON + listed",
    )

    # Arm OFF — neither should be killed on respawn
    disarm = {
        "hard_block_armed": False,
        "gate_locked": False,
        "lock_mode": "none",  # must clear strong lock or ApplyStickyLock re-arms
        "exes": ["calt_test_block_target.exe"],
        "provided_unlock": pwd,
        "unlock_password": pwd,
    }
    ok2, out2 = gateway_cmd("arm.set", disarm, timeout_ms=8000)
    add(rep, id="arm.live.disarm", layer="arm_live", ok=ok2, expected="ok", actual=out2[:200])
    if ok2:
        # Kill leftovers then respawn block target — must stay alive when disarmed
        subprocess.run(["taskkill", "/F", "/IM", "calt_test_block_target.exe"], capture_output=True)
        subprocess.run(["taskkill", "/F", "/IM", "calt_test_safe.exe"], capture_output=True)
        time.sleep(0.5)
        subprocess.Popen([str(BLOCK_TARGET)], cwd=str(FIXTURE_DIR))
        time.sleep(3.0)
        alive = process_alive("calt_test_block_target.exe")
        add(
            rep,
            id="arm.live.disarmed_no_kill",
            layer="arm_live",
            ok=alive,
            expected=True,
            actual=alive,
            detail="Arm OFF must not kill listed exe",
        )

    # Cleanup fixture processes
    for name in ("calt_test_block_target.exe", "calt_test_safe.exe"):
        subprocess.run(["taskkill", "/F", "/IM", name], capture_output=True)
    try:
        safe_p.kill()
    except Exception:
        pass
    try:
        block_p.kill()
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Report writers
# ---------------------------------------------------------------------------


def write_json_report(path: Path, rep: SuiteReport) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "started_at": rep.started_at,
        "finished_at": rep.finished_at,
        "passed": rep.passed,
        "failed": rep.failed,
        "skipped": rep.skipped,
        "gateway_ok": rep.gateway_ok,
        "restored": rep.restored,
        "notes": rep.notes,
        "results": [asdict(r) for r in rep.results],
    }
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def write_md_report(path: Path, rep: SuiteReport) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines: list[str] = []
    lines.append("# SoftLand / Arm comprehensive block-mode suite")
    lines.append("")
    lines.append(f"**When:** {rep.started_at} → {rep.finished_at}")
    lines.append(f"**Result:** {rep.passed} PASS / {rep.failed} FAIL / {rep.skipped} SKIP")
    lines.append(f"**Gateway:** {'ok' if rep.gateway_ok else 'down/timeout'}")
    lines.append(f"**Snapshot restored:** {rep.restored}")
    lines.append("")
    lines.append("## How to run")
    lines.append("")
    lines.append("```bat")
    lines.append("rem Fixture SoftLand + Arm unit + sync (safe)")
    lines.append("python scripts\\desktop_tracker\\run\\block_mode_suite.py")
    lines.append("")
    lines.append("rem + live probes; may restart wedged console enforcer; Arm kills if --arm-kills")
    lines.append("python scripts\\desktop_tracker\\run\\block_mode_suite.py --live --arm-kills")
    lines.append("")
    lines.append("rem Rebuild disposable Arm EXEs")
    lines.append("scripts\\desktop_tracker\\test_fixtures\\build_fixtures.bat")
    lines.append("```")
    lines.append("")
    lines.append("## Matrix coverage")
    lines.append("")
    lines.append("| Axis | Covered |")
    lines.append("|------|---------|")
    lines.append("| SoftLand ON/OFF | yes (fixture) |")
    lines.append("| STUDY / FREE / INCUBATION | yes (fixture) |")
    lines.append("| porn / watch / block_extra / allow / not_listed | yes |")
    lines.append("| www / subdomain / http / path / localhost / IP / empty URL | yes |")
    lines.append("| Arm match unit (case, basename, protected) | yes |")
    lines.append("| Arm live kill / safe misfire / disarm | via `--arm-kills` |")
    lines.append("| SQLite vs JSON sync | yes |")
    lines.append("")
    if rep.notes:
        lines.append("## Notes")
        lines.append("")
        for n in rep.notes:
            lines.append(f"- {n}")
        lines.append("")

    by_layer: dict[str, list[CaseResult]] = {}
    for r in rep.results:
        by_layer.setdefault(r.layer, []).append(r)

    for layer, cases in by_layer.items():
        fails = [c for c in cases if not c.ok and not c.skipped]
        lines.append(f"## {layer} ({sum(1 for c in cases if c.ok and not c.skipped)}/{len([c for c in cases if not c.skipped])} pass)")
        lines.append("")
        if fails:
            lines.append("| ID | expected | actual | detail |")
            lines.append("|----|----------|--------|--------|")
            for c in fails:
                lines.append(
                    f"| `{c.id}` | `{json.dumps(c.expected)[:80]}` | `{json.dumps(c.actual)[:80]}` | {c.detail} |"
                )
            lines.append("")
        else:
            lines.append("All cases in this layer passed (or skipped).")
            lines.append("")

    lines.append("## Product reminder")
    lines.append("")
    lines.append("SoftLand ON ≠ Arm. SoftLand blocks sites; Arm kills processes.")
    lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description="CALT Focus SoftLand/Arm block-mode suite")
    ap.add_argument("--live", action="store_true", help="Live SoftLand probes + gateway checks")
    ap.add_argument("--arm-kills", action="store_true", help="Live Arm tests with fixture EXEs (needs gateway)")
    ap.add_argument("--no-restart", action="store_true", help="Do not restart enforcer if pipe down")
    ap.add_argument("--json", type=Path, default=None, help="Write JSON results")
    ap.add_argument(
        "--md",
        type=Path,
        default=REPO / "docs" / "superpowers" / "exports" / "2026-09-13-block-mode-suite-report.md",
        help="Write markdown report",
    )
    args = ap.parse_args()

    rep = SuiteReport(started_at=now_iso())
    snap = take_snapshot()
    mutated = False
    gateway_ok = False

    try:
        run_softland_fixture_matrix(rep)
        run_arm_unit(rep)
        run_sync_checks(rep, phase="pre")

        if args.live or args.arm_kills:
            run_live_softland_probes(rep)
            ok, out = gateway_cmd("status.snapshot", timeout_ms=5000)
            gateway_ok = ok
            add(
                rep,
                id="gateway.status.snapshot",
                layer="gateway",
                ok=ok,
                expected="ok",
                actual=out[:400],
            )
            if not ok and not args.no_restart:
                soft_off = True
                if snap.softland is not None:
                    soft_off = not bool(snap.softland.get("softland_enabled"))
                # Safe-ish: SoftLand already off on owner machine; console restart
                if soft_off:
                    rep.notes.append("Restarting wedged calt_enforcer --console to recover gateway")
                    gateway_ok = restart_enforcer_console(rep)
                else:
                    rep.notes.append("Skipped enforcer restart because SoftLand was ON")

            rep.gateway_ok = gateway_ok

            if args.arm_kills:
                if not gateway_ok:
                    add(
                        rep,
                        id="arm.live.skipped_no_gateway",
                        layer="arm_live",
                        ok=False,
                        expected="gateway",
                        actual="down",
                        skipped=True,
                        detail="cannot safely mutate Arm without gateway",
                    )
                else:
                    mutated = True
                    run_arm_live_kills(rep, snap)
        else:
            # Still try a non-mutating gateway ping for the report
            ok, out = gateway_cmd("status.snapshot", timeout_ms=4000)
            rep.gateway_ok = ok
            add(
                rep,
                id="gateway.status.snapshot",
                layer="gateway",
                ok=ok,
                expected="ok",
                actual=out[:400],
                skipped=not ok,
                detail="" if ok else "pipe timeout (use --live to attempt restart)",
            )

    finally:
        if mutated or args.arm_kills:
            restore_snapshot(snap, rep, via_gateway=rep.gateway_ok or gateway_ok)
            # After gateway arm writes, SQLite should match JSON (SyncEnforcerRuntimeSqlite).
            time.sleep(0.5)
            run_sync_checks(rep, phase="post")
        else:
            # No mutations — mark restored as N/A success
            rep.restored = True
            add(
                rep,
                id="restore.snapshot",
                layer="gateway",
                ok=True,
                expected="unchanged",
                actual="no live Arm mutations",
                detail="SoftLand/Arm left as found",
            )
        # Ensure fixture processes gone
        for name in ("calt_test_block_target.exe", "calt_test_safe.exe"):
            subprocess.run(["taskkill", "/F", "/IM", name], capture_output=True)

    rep.finished_at = now_iso()

    json_path = args.json or (
        REPO / "docs" / "superpowers" / "exports" / "2026-09-13-block-mode-suite-results.json"
    )
    write_json_report(json_path, rep)
    write_md_report(args.md, rep)

    print(f"PASS={rep.passed} FAIL={rep.failed} SKIP={rep.skipped}")
    print(f"gateway_ok={rep.gateway_ok} restored={rep.restored}")
    print(f"json={json_path}")
    print(f"md={args.md}")
    for r in rep.results:
        if not r.ok and not r.skipped:
            print(f"FAIL {r.id}: expected={r.expected!r} actual={r.actual!r} {r.detail}")
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())
