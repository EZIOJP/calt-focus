"""Arm unit + live kill cases (edges / negatives)."""
from __future__ import annotations

import time
import subprocess
from typing import Callable

from . import error_codes as EC
from .model import StepResult, SuiteReport

from .bms_loader import load_bms

bms = load_bms()


ARM_EXTRA = [
    # (proc, policy_exes, expect_kill, category, title)
    ("Steam.exe", ["steam.exe"], True, "edge", "Mixed-case process name"),
    ("steam.exe", ["STEAM.EXE"], True, "edge", "Mixed-case policy token"),
    ("chrome.exe", ["steam.exe"], False, "negative", "NEGATIVE: unlisted chrome must not match"),
    ("steamwebhelper.exe", ["steam.exe"], False, "negative", "NEGATIVE: similar name must not fuzzy-match"),
    ("", ["steam.exe"], False, "edge", "Empty process name"),
    ("steam.exe", [], False, "negative", "NEGATIVE: empty policy list never kills"),
    ("steam.exe", [""], False, "edge", "Empty policy token ignored"),
    ("calt_focus.exe", ["calt_focus.exe"], False, "negative", "NEGATIVE: protected Focus never killed"),
    (r"C:\Games\steam.exe", ["steam.exe"], True, "edge", "Full path process basename"),
]


def run_arm_unit(rep: SuiteReport) -> None:
    for proc, exes, expect in bms.ARM_UNIT_CASES:
        got = bms.would_kill(proc, exes)
        ok = got == expect
        steps = [
            StepResult(1, f"Normalize process {proc!r}", "token", bms.normalize_exe_token(proc), True),
            StepResult(2, f"Match against {exes}", str(expect), str(got), ok),
        ]
        rep.add(
            id=f"arm.unit.{proc}->{exes}",
            title=f"Arm match: {proc} vs {exes}",
            suite="arm",
            category="positive",
            ok=ok,
            error_code=EC.E200 if not ok else EC.OK,
            expected=expect,
            actual=got,
            steps=steps,
            tags=["arm", "unit"],
        )
    for proc, exes, expect, cat, title in ARM_EXTRA:
        got = bms.would_kill(proc, exes)
        ok = got == expect
        steps = [
            StepResult(1, f"Normalize {proc!r}", "token", bms.normalize_exe_token(proc), True),
            StepResult(2, f"Match vs {exes}", str(expect), str(got), ok),
        ]
        rep.add(
            id=f"arm.unit.extra.{title}",
            title=title,
            suite="arm",
            category=cat,
            ok=ok,
            error_code=EC.E200 if not ok else EC.OK,
            expected=expect,
            actual=got,
            steps=steps,
            tags=["arm", "unit", cat],
        )


def run_arm_live(rep: SuiteReport, snap, on_fail_shot: Callable[[str], list[str]] | None = None) -> None:
    if not bms.BLOCK_TARGET.exists() or not bms.SAFE_TARGET.exists():
        rep.add(
            id="arm.live.fixtures_built",
            title="Disposable Arm fixture EXEs present",
            suite="arm",
            category="positive",
            ok=False,
            error_code=EC.E900,
            expected=True,
            actual=False,
            detail="run scripts/desktop_tracker/test_fixtures/build_fixtures.bat",
            skipped=False,
            steps=[StepResult(1, "Check fixture EXEs", "present", "missing", False)],
        )
        return

    pwd = snap.unlock_password
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

    cid_set = "arm.live.set_fixture_list"
    steps = [StepResult(1, "gateway arm.set fixture list + Arm ON", "ok", "", True)]
    ok, out = bms.gateway_cmd("arm.set", payload, timeout_ms=8000)
    steps[-1].actual = out[:200]
    steps[-1].ok = ok
    shots = on_fail_shot(cid_set) if (not ok and on_fail_shot) else []
    rep.add(
        id=cid_set,
        title="Arm live: set disposable kill list",
        suite="arm",
        category="positive",
        ok=ok,
        error_code=EC.E300 if not ok else EC.OK,
        expected="arm.set ok",
        actual=out[:300],
        steps=steps,
        screenshots=shots,
        tags=["arm", "live"],
    )
    if not ok:
        return

    time.sleep(1.0)
    safe_p = subprocess.Popen([str(bms.SAFE_TARGET)], cwd=str(bms.FIXTURE_DIR))
    block_p = subprocess.Popen([str(bms.BLOCK_TARGET)], cwd=str(bms.FIXTURE_DIR))
    time.sleep(3.5)

    safe_alive = bms.process_alive("calt_test_safe.exe")
    block_alive = bms.process_alive("calt_test_block_target.exe")

    cid_safe = "arm.live.safe_not_killed"
    ok_safe = safe_alive
    shots = on_fail_shot(cid_safe) if (not ok_safe and on_fail_shot) else []
    rep.add(
        id=cid_safe,
        title="NEGATIVE: unlisted safe EXE must survive Arm",
        suite="arm",
        category="negative",
        ok=ok_safe,
        error_code=EC.E211 if not ok_safe else EC.OK,
        expected=True,
        actual=safe_alive,
        steps=[
            StepResult(1, "Launch calt_test_safe.exe", "alive", str(safe_alive), ok_safe),
            StepResult(2, "Wait enforcer tick", "not killed", "alive" if safe_alive else "dead", ok_safe),
        ],
        screenshots=shots,
        tags=["arm", "live", "negative"],
    )

    cid_kill = "arm.live.block_target_killed"
    ok_kill = not block_alive
    shots = on_fail_shot(cid_kill) if (not ok_kill and on_fail_shot) else []
    rep.add(
        id=cid_kill,
        title="Arm ON kills listed fixture target",
        suite="arm",
        category="positive",
        ok=ok_kill,
        error_code=EC.E210 if not ok_kill else EC.OK,
        expected=False,
        actual=block_alive,
        steps=[
            StepResult(1, "Launch calt_test_block_target.exe", "listed", "launched", True),
            StepResult(2, "Wait enforcer tick", "killed", "alive" if block_alive else "killed", ok_kill),
        ],
        screenshots=shots,
        tags=["arm", "live"],
    )

    disarm = {
        "hard_block_armed": False,
        "gate_locked": False,
        "lock_mode": "none",
        "exes": ["calt_test_block_target.exe"],
        "provided_unlock": pwd,
        "unlock_password": pwd,
    }
    ok2, out2 = bms.gateway_cmd("arm.set", disarm, timeout_ms=8000)
    rep.add(
        id="arm.live.disarm",
        title="Arm live: disarm (clear lock_mode)",
        suite="arm",
        category="positive",
        ok=ok2,
        error_code=EC.E300 if not ok2 else EC.OK,
        expected="ok",
        actual=out2[:200],
        steps=[StepResult(1, "arm.set hard_block_armed=false lock_mode=none", "ok", out2[:120], ok2)],
        tags=["arm", "live"],
    )
    if ok2:
        subprocess.run(["taskkill", "/F", "/IM", "calt_test_block_target.exe"], capture_output=True)
        subprocess.run(["taskkill", "/F", "/IM", "calt_test_safe.exe"], capture_output=True)
        time.sleep(0.5)
        subprocess.Popen([str(bms.BLOCK_TARGET)], cwd=str(bms.FIXTURE_DIR))
        time.sleep(3.0)
        alive = bms.process_alive("calt_test_block_target.exe")
        cid = "arm.live.disarmed_no_kill"
        shots = on_fail_shot(cid) if (not alive and on_fail_shot) else []
        rep.add(
            id=cid,
            title="NEGATIVE: Arm OFF must not kill listed EXE",
            suite="arm",
            category="negative",
            ok=alive,
            error_code=EC.E210 if not alive else EC.OK,
            expected=True,
            actual=alive,
            steps=[
                StepResult(1, "Disarm", "disarmed", "ok", True),
                StepResult(2, "Respawn listed target", "stays alive", str(alive), alive),
            ],
            screenshots=shots,
            tags=["arm", "live", "negative"],
        )

    # Wrong-password negative (should not disarm sticky)
    if pwd:
        arm_pw = {
            "hard_block_armed": True,
            "gate_locked": True,
            "lock_mode": "password",
            "exes": ["calt_test_block_target.exe"],
            "provided_unlock": pwd,
            "unlock_password": pwd,
        }
        bad = {
            "hard_block_armed": False,
            "gate_locked": False,
            "lock_mode": "none",
            "exes": ["calt_test_block_target.exe"],
            "provided_unlock": "WRONG-PASSWORD-FOCUS-QA",
            "unlock_password": pwd,
        }
        bms.gateway_cmd("arm.set", arm_pw, timeout_ms=8000)
        time.sleep(0.5)
        ok_bad, out_bad = bms.gateway_cmd("arm.set", bad, timeout_ms=8000)
        armed_still = True
        try:
            pol = bms.read_json(bms.BEH / "enforcer_policy.json")
            armed_still = bool(pol.get("hard_block_armed"))
        except Exception:
            pass
        ok_neg = (not ok_bad) or armed_still
        cid = "arm.live.wrong_password_no_disarm"
        shots = on_fail_shot(cid) if (not ok_neg and on_fail_shot) else []
        rep.add(
            id=cid,
            title="NEGATIVE: wrong unlock password must not disarm",
            suite="arm",
            category="negative",
            ok=ok_neg,
            error_code=EC.E302 if not ok_neg else EC.OK,
            expected="reject or stay armed",
            actual={"gateway_ok": ok_bad, "out": out_bad[:200], "hard_block_armed": armed_still},
            steps=[
                StepResult(1, "Arm ON with lock_mode=password", "armed", "armed", True),
                StepResult(2, "arm.set disarm with wrong provided_unlock", "reject/stay armed", f"ok={ok_bad} armed={armed_still}", ok_neg),
            ],
            screenshots=shots,
            tags=["arm", "live", "negative", "unlock"],
        )
        # Ensure we end disarmed for restore friendliness (correct pwd)
        bms.gateway_cmd(
            "arm.set",
            {
                "hard_block_armed": False,
                "gate_locked": False,
                "lock_mode": "none",
                "exes": ["calt_test_block_target.exe"],
                "provided_unlock": pwd,
                "unlock_password": pwd,
            },
            timeout_ms=8000,
        )
    else:
        rep.add(
            id="arm.live.wrong_password_no_disarm",
            title="NEGATIVE: wrong unlock password must not disarm",
            suite="arm",
            category="negative",
            ok=True,
            skipped=True,
            expected="password set",
            actual="no unlock_password in snapshot",
            detail="skipped — no password configured",
            tags=["arm", "live", "negative", "unlock"],
        )

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
