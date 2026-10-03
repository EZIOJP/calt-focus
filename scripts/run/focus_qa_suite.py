#!/usr/bin/env python3
"""
Focus QA suite — SoftLand + Arm + gateway + UI smoke with rich reports.

  python scripts/desktop_tracker/run/focus_qa_suite.py
  python scripts/desktop_tracker/run/focus_qa_suite.py --live --arm-kills --ui

Reports:
  docs/superpowers/exports/YYYY-MM-DD-focus-qa-report.md
  docs/superpowers/exports/YYYY-MM-DD-focus-qa-results.json
  docs/superpowers/exports/YYYY-MM-DD-focus-qa-report.html
  docs/superpowers/exports/focus-qa-artifacts/screenshots/
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow importing focus_qa package next to this file
RUN_DIR = Path(__file__).resolve().parent
if str(RUN_DIR) not in sys.path:
    sys.path.insert(0, str(RUN_DIR))

from focus_qa.model import SuiteReport, now_iso  # noqa: E402
from focus_qa.report import write_html, write_json, write_markdown  # noqa: E402
from focus_qa.screenshots import capture_screen, shot_path  # noqa: E402
from focus_qa import softland, arm, gateway, ui  # noqa: E402

from focus_qa.bms_loader import load_bms  # noqa: E402

bms = load_bms()


def find_repo() -> Path:
    return bms.REPO


def main() -> int:
    ap = argparse.ArgumentParser(description="Focus QA — SoftLand/Arm/gateway/UI")
    ap.add_argument("--live", action="store_true", help="Live SoftLand + gateway mutations path")
    ap.add_argument("--arm-kills", action="store_true", help="Live Arm fixture kills (needs gateway)")
    ap.add_argument("--ui", action="store_true", help="Serve dist-focus and smoke hash routes + screenshots")
    ap.add_argument("--no-restart", action="store_true", help="Do not restart wedged enforcer")
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--md", type=Path, default=None)
    ap.add_argument("--html", type=Path, default=None)
    ap.add_argument("--artifacts", type=Path, default=None)
    args = ap.parse_args()

    repo = find_repo()
    day = now_iso()[:10]
    exports = repo / "docs" / "superpowers" / "exports"
    artifact_dir = args.artifacts or (exports / "focus-qa-artifacts")
    artifact_dir.mkdir(parents=True, exist_ok=True)
    json_path = args.json or (exports / f"{day}-focus-qa-results.json")
    md_path = args.md or (exports / f"{day}-focus-qa-report.md")
    html_path = args.html or (exports / f"{day}-focus-qa-report.html")

    rep = SuiteReport(started_at=now_iso(), artifact_dir=str(artifact_dir))

    def on_fail_shot(case_id: str) -> list[str]:
        dest = shot_path(artifact_dir, case_id, "fail")
        p = capture_screen(dest, case_id)
        return [p] if p else []

    # --- SoftLand fixture (always) ---
    softland.run_softland_fixture(rep, on_fail_shot=on_fail_shot)

    # --- Arm unit (always) ---
    arm.run_arm_unit(rep)

    # --- Sync pre ---
    gateway.run_sync(rep, phase="pre")

    snap = None
    via_gw = False
    if args.live or args.arm_kills:
        snap = bms.take_snapshot()
        ok, out = bms.gateway_cmd("status.snapshot", timeout_ms=6000)
        if not ok and not args.no_restart:
            rep.notes.append("Gateway down — attempting enforcer console restart")
            shim2 = bms.SuiteReport(started_at=rep.started_at, notes=list(rep.notes))
            ok = bms.restart_enforcer_console(shim2)
            for r in shim2.results:
                rep.add(
                    id=r.id,
                    title=r.id,
                    suite="gateway",
                    category="positive",
                    ok=r.ok,
                    error_code="E300" if not r.ok else "OK",
                    expected=r.expected,
                    actual=r.actual,
                    detail=r.detail,
                    skipped=r.skipped,
                )
            for n in shim2.notes:
                if n not in rep.notes:
                    rep.notes.append(n)
            ok, out = bms.gateway_cmd("status.snapshot", timeout_ms=6000)

        via_gw = ok
        rep.gateway_ok = ok
        if ok:
            gateway_ok = gateway.run_gateway_suite(rep, on_fail_shot=on_fail_shot)
            rep.gateway_ok = gateway_ok or ok
            softland.run_softland_live(rep, on_fail_shot=on_fail_shot)
            if args.arm_kills:
                arm.run_arm_live(rep, snap, on_fail_shot=on_fail_shot)
            # post sync runs after restore settle below
        else:
            rep.add(
                id="gateway.status.snapshot",
                title="Gateway status.snapshot (live)",
                suite="gateway",
                category="positive",
                ok=False,
                error_code="E300",
                expected="ok",
                actual=out[:300],
                detail="pipe timeout — start calt_enforcer",
            )
            rep.notes.append("Live/gateway suites skipped — enforcer pipe down")
    else:
        # Safe mode: still try a read-only gateway probe (non-fatal)
        ok, out = bms.gateway_cmd("status.snapshot", timeout_ms=4000)
        rep.gateway_ok = ok
        if ok:
            gateway.run_gateway_suite(rep, on_fail_shot=on_fail_shot)
        else:
            rep.add(
                id="gateway.status.snapshot",
                title="Gateway status.snapshot (optional in safe mode)",
                suite="gateway",
                category="positive",
                ok=True,
                skipped=True,
                expected="ok",
                actual=out[:200],
                detail="skipped — start enforcer or pass --live",
            )

    if args.ui:
        ui.run_ui_smoke(rep, artifact_dir, on_fail_shot=on_fail_shot)

    # Restore if we snapped
    if snap is not None and (args.live or args.arm_kills):
        # Use bms restore into shim then copy
        shim = bms.SuiteReport(started_at=rep.started_at)
        bms.restore_snapshot(snap, shim, via_gateway=via_gw)
        rep.restored = shim.restored
        rep.notes.extend(shim.notes)
        for r in shim.results:
            rep.add(
                id=r.id,
                title="Restore SoftLand/Arm snapshot",
                suite="gateway",
                category="positive",
                ok=r.ok,
                error_code="E300" if not r.ok else "OK",
                expected=r.expected,
                actual=r.actual,
                detail=r.detail,
            )
        # Allow status mirror to settle after restore / password tests
        import time as _time

        _time.sleep(1.5)
        gateway.run_sync(rep, phase="post")
    else:
        rep.restored = True  # nothing mutated

    rep.finished_at = now_iso()
    write_json(json_path, rep)
    write_markdown(md_path, rep)
    write_html(html_path, rep)

    print(f"PASS={rep.passed} FAIL={rep.failed} SKIP={rep.skipped}")
    print(f"JSON: {json_path}")
    print(f"MD:   {md_path}")
    print(f"HTML: {html_path}")
    print(f"Arts: {artifact_dir}")
    return 1 if rep.failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
