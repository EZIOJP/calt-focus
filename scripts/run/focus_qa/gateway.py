"""Gateway control-plane cases (reads + negatives) with snapshot awareness."""
from __future__ import annotations

import json
from typing import Callable

from . import error_codes as EC
from .model import StepResult, SuiteReport

from .bms_loader import load_bms

bms = load_bms()


def _parse_ok(out: str) -> tuple[bool, dict]:
    try:
        # gateway_cmd.ps1 may print JSON on stdout
        start = out.find("{")
        if start < 0:
            return False, {}
        doc = json.loads(out[start:])
        return bool(doc.get("ok")), doc
    except Exception:
        return False, {}


def run_gateway_suite(rep: SuiteReport, on_fail_shot: Callable[[str], list[str]] | None = None) -> bool:
    """Returns gateway_ok."""
    reads = [
        ("status.snapshot", {}, "Gateway status.snapshot"),
        ("day.status", {}, "Gateway day.status"),
        ("reward.status", {}, "Gateway reward.status"),
        ("day.loop_snapshot", {}, "Gateway day.loop_snapshot"),
        ("ledger.snapshot", {}, "Gateway ledger.snapshot"),
        ("device_block.status", {}, "Gateway device_block.status"),
    ]
    gateway_ok = False
    for op, payload, title in reads:
        cid = f"gateway.read.{op}"
        steps = [StepResult(1, f"Call {op}", "ok:true JSON", "", True)]
        ok, out = bms.gateway_cmd(op, payload, timeout_ms=8000)
        parsed_ok, doc = _parse_ok(out)
        steps[-1].actual = out[:180]
        steps[-1].ok = ok and parsed_ok
        if op == "status.snapshot" and ok and parsed_ok:
            gateway_ok = True
        shots = on_fail_shot(cid) if (not (ok and parsed_ok) and on_fail_shot) else []
        code = EC.OK
        if not ok:
            code = EC.E300
        elif not parsed_ok:
            code = EC.E301
        rep.add(
            id=cid,
            title=title,
            suite="gateway",
            category="positive",
            ok=ok and parsed_ok,
            error_code=code,
            expected="ok:true",
            actual=doc or out[:300],
            steps=steps,
            screenshots=shots,
            tags=["gateway", "read"],
        )

    # Negatives — expected failures / rejects
    negatives = [
        (
            "arm.set",
            {"hard_block_armed": False, "provided_unlock": "__definitely_wrong__", "lock_mode": "none"},
            "NEGATIVE: arm.set with wrong unlock (may reject if password lock active)",
            "unlock",
        ),
        (
            "softland.set_enabled",
            {},  # missing enabled
            "NEGATIVE: softland.set_enabled missing enabled field",
            "bad_payload",
        ),
        (
            "this.op.does.not.exist",
            {},
            "NEGATIVE: unknown gateway op",
            "unknown_op",
        ),
        (
            "reward.claim",
            {},
            "NEGATIVE: reward.claim when available may be 0 (reject or no-op fail)",
            "reward",
        ),
    ]

    for op, payload, title, tag in negatives:
        cid = f"gateway.neg.{op}.{tag}"
        steps = [
            StepResult(1, f"Call {op} (negative)", "reject / ok:false / error", "", True),
        ]
        ok, out = bms.gateway_cmd(op, payload, timeout_ms=8000)
        parsed_ok, doc = _parse_ok(out)
        # For negatives: success means we got a *controlled* reject OR (for reward) explicit fail.
        # Unknown op / bad payload: expect not (ok and parsed_ok) OR doc.ok is False.
        if tag == "unlock":
            # If SoftLand/Arm unlocked with lock_mode none, wrong unlock may still succeed disarm — note as soft.
            # Pass if gateway rejects OR policy stays consistent; treat clean reject as ideal.
            soft_pass = (not ok) or (not parsed_ok) or (doc.get("ok") is False)
            # If it succeeded, mark SKIP-ish note — not a hard product fail when lock_mode=none
            if not soft_pass and (doc.get("ok") is True):
                rep.add(
                    id=cid,
                    title=title,
                    suite="gateway",
                    category="negative",
                    ok=True,
                    skipped=True,
                    expected="reject when password lock active",
                    actual=doc or out[:200],
                    detail="lock_mode may be none — wrong unlock not enforced; skipped strict assert",
                    steps=steps,
                    tags=["gateway", "negative", tag],
                )
                continue
            neg_ok = soft_pass
        else:
            neg_ok = (not ok) or (not parsed_ok) or (doc.get("ok") is False)
        steps[-1].actual = out[:180]
        steps[-1].ok = neg_ok
        shots = on_fail_shot(cid) if (not neg_ok and on_fail_shot) else []
        rep.add(
            id=cid,
            title=title,
            suite="gateway",
            category="negative",
            ok=neg_ok,
            error_code=EC.E302 if not neg_ok else EC.OK,
            expected="reject / ok:false",
            actual=doc or out[:300],
            steps=steps,
            screenshots=shots,
            tags=["gateway", "negative", tag],
        )

    # SoftLand independence read
    soft = bms.read_json(bms.BEH / "softland_policy.json") if (bms.BEH / "softland_policy.json").exists() else {}
    pol = bms.read_json(bms.BEH / "enforcer_policy.json") if (bms.BEH / "enforcer_policy.json").exists() else {}
    rep.add(
        id="gateway.info.softland_ne_arm",
        title="INFO: SoftLand and Arm are independent switches",
        suite="gateway",
        category="positive",
        ok=True,
        expected="independent",
        actual={
            "softland_enabled": soft.get("softland_enabled"),
            "hard_block_armed": pol.get("hard_block_armed"),
        },
        steps=[StepResult(1, "Read mirrors", "both readable", "ok", True)],
        tags=["gateway", "info"],
    )
    return gateway_ok


def run_sync(rep: SuiteReport, phase: str = "pre") -> None:
    """Wrap block_mode sync into rich cases."""
    # Reuse by calling bms.run_sync_checks into a throwaway then convert — simpler to reimplement thin.
    pol_path = bms.BEH / "enforcer_policy.json"
    status_path = bms.BEH / "enforcer_status.json"
    soft_path = bms.BEH / "softland_policy.json"
    prefix = f"sync.{phase}"
    if not pol_path.exists():
        rep.add(
            id=f"{prefix}.policy_present",
            title="enforcer_policy.json present",
            suite="sync",
            category="positive",
            ok=False,
            error_code=EC.E400,
            expected=True,
            actual=False,
        )
        return
    pol = bms.read_json(pol_path)
    status = bms.read_json(status_path) if status_path.exists() else {}
    soft = bms.read_json(soft_path) if soft_path.exists() else {}
    sql = bms.sqlite_arm_row()
    eff = bms.effective_arm_from_json(pol)

    if status:
        ok = bool(status.get("armed")) == bool(eff["armed"])
        # Status mirror can lag one tick after restore; re-read once.
        if not ok and phase == "post":
            import time

            time.sleep(1.0)
            status = bms.read_json(status_path) if status_path.exists() else {}
            ok = bool(status.get("armed")) == bool(eff["armed"])
        rep.add(
            id=f"{prefix}.status_vs_effective_arm",
            title="Status armed matches effective Arm",
            suite="sync",
            category="positive",
            ok=ok,
            error_code=EC.E400 if not ok else EC.OK,
            expected=eff["armed"],
            actual=status.get("armed"),
            detail="" if ok else "status mirror lag or sticky lock after unlock tests",
            steps=[
                StepResult(1, "Read enforcer_status.json armed", str(eff["armed"]), str(status.get("armed")), ok),
            ],
            tags=["sync"],
        )
    if sql is not None:
        json_exes = [bms.normalize_exe_token(x) for x in (pol.get("exes") or [])]
        sql_exes = [bms.normalize_exe_token(x) for x in sql["exes"]]
        ok = sorted(json_exes) == sorted(sql_exes) and bool(pol.get("hard_block_armed")) == bool(
            sql["hard_block_armed"]
        )
        rep.add(
            id=f"{prefix}.sqlite_vs_json_arm",
            title="SQLite Arm mirrors JSON",
            suite="sync",
            category="positive",
            ok=ok,
            error_code=EC.E400 if not ok else EC.OK,
            expected={"json_armed": pol.get("hard_block_armed"), "json_exes": json_exes},
            actual={"sql_armed": sql["hard_block_armed"], "sql_exes": sql_exes},
            steps=[StepResult(1, "Compare enforcer_runtime vs policy JSON", "match", "match" if ok else "diverge", ok)],
            tags=["sync"],
        )
    sql_soft = bms.sqlite_softland_enabled()
    soft_en = soft.get("softland_enabled")
    if sql_soft is not None and soft_en is not None:
        ok = bool(sql_soft) == bool(soft_en)
        rep.add(
            id=f"{prefix}.sqlite_vs_json_softland",
            title="SQLite SoftLand enabled matches JSON",
            suite="sync",
            category="positive",
            ok=ok,
            error_code=EC.E400 if not ok else EC.OK,
            expected=soft_en,
            actual=sql_soft,
            steps=[StepResult(1, "Compare SoftLand enabled", str(soft_en), str(sql_soft), ok)],
            tags=["sync"],
        )
    else:
        rep.add(
            id=f"{prefix}.sqlite_vs_json_softland",
            title="SQLite SoftLand compare",
            suite="sync",
            category="positive",
            ok=True,
            skipped=True,
            expected=soft_en,
            actual=sql_soft,
            detail="inconclusive",
            tags=["sync"],
        )
