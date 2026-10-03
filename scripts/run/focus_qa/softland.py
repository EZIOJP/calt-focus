"""SoftLand fixture + live + edge/negative cases."""
from __future__ import annotations

import json
import shutil
import tempfile
from pathlib import Path
from typing import Any, Callable

from . import error_codes as EC
from .model import StepResult, SuiteReport

from .bms_loader import load_bms

bms = load_bms()


# Extra edges + negatives beyond base SOFTLAND_CASES
EXTRA_SOFTLAND: list[dict[str, Any]] = [
    # --- edges ---
    {"cell": "study", "url": "HTTPS://YouTube.COM/Watch?V=1", "action": "block", "reason": "watch_list", "category": "edge", "title": "Case-insensitive YouTube host"},
    {"cell": "study", "url": "https://youtube.com.", "action": "allow", "reason": "not_listed", "category": "edge", "title": "EDGE: trailing-dot host currently not_listed (not watch_list)"},
    {"cell": "study", "url": "https://youtube.com:443/watch", "action": "block", "reason": "watch_list", "category": "edge", "title": "Explicit :443 port"},
    {"cell": "study", "url": "https://user:pass@youtube.com/", "action": "allow", "reason": "not_listed", "category": "edge", "title": "EDGE: userinfo URL currently not_listed (host parse)"},
    {"cell": "study", "url": "https://www.www.youtube.com", "action": "block", "reason": "watch_list", "category": "edge", "title": "Double-www subdomain"},
    {"cell": "study", "url": "https://notyoutube.com", "action": "allow", "reason": "not_listed", "category": "edge", "title": "Suffix lookalike notyoutube.com"},
    {"cell": "study", "url": "https://youtube.com.evil.test", "action": "allow", "reason": "not_listed", "category": "edge", "title": "Evil suffix after youtube.com"},
    {"cell": "study", "url": "javascript:alert(1)", "action": "allow", "reason_in": ["no_host", "not_listed", "softland_off"], "category": "edge", "title": "javascript: URL"},
    {"cell": "study", "url": "data:text/html,hi", "action": "allow", "reason_in": ["no_host", "not_listed"], "category": "edge", "title": "data: URL"},
    {"cell": "study", "url": "about:blank", "action": "allow", "reason_in": ["no_host", "not_listed"], "category": "edge", "title": "about:blank"},
    {"cell": "study", "url": "   ", "action": "allow", "reason_in": ["no_host", "not_listed"], "category": "edge", "title": "Whitespace-only URL"},
    {"cell": "study", "url": "https://xn--n3h.com", "action": "allow", "reason": "not_listed", "category": "edge", "title": "Punycode host"},
    # --- negatives (document expected block; if allow = product bug) ---
    {"cell": "free", "url": "https://pornhub.com", "action": "block", "reason": "porn", "category": "negative", "title": "NEGATIVE: porn stays blocked in FREE"},
    {"cell": "free", "url": "https://twitter.com", "action": "block", "reason": "block_extra", "category": "negative", "title": "NEGATIVE: block_extra stays blocked in FREE"},
    {"cell": "off", "url": "https://pornhub.com", "action": "allow", "reason": "softland_off", "category": "negative", "title": "NEGATIVE: SoftLand OFF must allow porn (no SoftLand block)"},
    {"cell": "incubate", "url": "https://youtube.com", "action": "block", "reason": "incubation", "category": "negative", "title": "NEGATIVE: incubation overrides FREE watch"},
]


def _all_cases() -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for c in bms.SOFTLAND_CASES:
        d = dict(c)
        d.setdefault("category", "positive")
        d.setdefault("title", f"SoftLand {c['cell']}: {c['url'] or 'empty'}")
        out.append(d)
    out.extend(EXTRA_SOFTLAND)
    return out


def run_softland_fixture(rep: SuiteReport, on_fail_shot: Callable[[str], list[str]] | None = None) -> None:
    if not bms.MSG_HOST.exists():
        rep.add(
            id="softland.msg_host_present",
            title="calt_msg_host.exe must exist",
            suite="softland",
            category="positive",
            ok=False,
            error_code=EC.E110,
            expected=True,
            actual=False,
            detail=str(bms.MSG_HOST),
            steps=[StepResult(1, "Locate calt_msg_host.exe", "exists", "missing", False)],
        )
        return

    tmp = Path(tempfile.mkdtemp(prefix="calt_focus_qa_sl_"))
    beh = tmp / "behavior"
    beh.mkdir(parents=True)
    try:
        current_cell = None
        for case in _all_cases():
            cell = case["cell"]
            url = case["url"]
            cid = f"softland.fixture.{cell}.{url or 'empty'}"
            steps: list[StepResult] = []
            steps.append(StepResult(1, f"Set fixture SoftLand cell={cell}", "policy written", "", True))
            if cell != current_cell:
                current_cell = cell
                doc = bms.doc_for_cell(cell)
                (beh / "softland_policy.json").write_text(json.dumps(doc), encoding="utf-8")
                steps[-1].actual = "written"
            else:
                steps[-1].actual = "reuse"
            steps.append(StepResult(2, f"msg_host get_mode({url!r})", str(case.get("action")), "", True))
            try:
                r = bms.msg_host_get_mode(url, tmp)
            except Exception as e:
                steps[-1].ok = False
                steps[-1].actual = str(e)[:200]
                shots = on_fail_shot(cid) if on_fail_shot else []
                rep.add(
                    id=cid,
                    title=case.get("title") or cid,
                    suite="softland",
                    category=case.get("category", "positive"),
                    ok=False,
                    error_code=EC.E110,
                    expected=case.get("action"),
                    actual=str(e),
                    detail="msg_host error",
                    steps=steps,
                    screenshots=shots,
                    tags=["fixture", "softland"],
                )
                continue

            action = r.get("action")
            reason = r.get("reason")
            action_ok = action == case["action"]
            if "reason" in case:
                reason_ok = reason == case["reason"]
            else:
                reason_ok = reason in case.get("reason_in", [])
            steps[-1].actual = f"{action}/{reason}"
            steps[-1].ok = action_ok and reason_ok
            steps.append(
                StepResult(
                    3,
                    "Compare action+reason",
                    json.dumps({"action": case["action"], "reason": case.get("reason") or case.get("reason_in")}),
                    json.dumps({"action": action, "reason": reason}),
                    action_ok and reason_ok,
                )
            )
            ok = action_ok and reason_ok
            code = EC.OK
            if not ok:
                code = EC.E100 if not action_ok else EC.E101
            shots = on_fail_shot(cid) if (not ok and on_fail_shot) else []
            rep.add(
                id=cid,
                title=case.get("title") or cid,
                suite="softland",
                category=case.get("category", "positive"),
                ok=ok,
                error_code=code,
                expected={"action": case["action"], "reason": case.get("reason") or case.get("reason_in")},
                actual={"action": action, "reason": reason, "mode": r.get("mode"), "enforce": r.get("enforce")},
                detail="" if ok else "SoftLand decide misfire",
                steps=steps,
                screenshots=shots,
                tags=["fixture", "softland", case.get("category", "positive")],
            )
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def run_softland_live(rep: SuiteReport, on_fail_shot: Callable[[str], list[str]] | None = None) -> None:
    soft = bms.read_json(bms.BEH / "softland_policy.json")
    enabled = bool(soft.get("softland_enabled"))
    probes = [
        ("https://youtube.com", "watch"),
        ("https://pornhub.com", "porn"),
        ("https://meet.google.com", "allow"),
        ("https://github.com", "other"),
        ("https://twitter.com", "block_extra"),
        ("", "empty"),
        ("not-a-url", "garbage"),
    ]
    for u, tag in probes:
        cid = f"softland.live.{u or 'empty'}"
        steps = [
            StepResult(1, "Read live softland_enabled", str(enabled), str(enabled), True),
            StepResult(2, f"Live get_mode({u!r})", "softland_off allow" if not enabled else "valid action", "", True),
        ]
        try:
            r = bms.live_msg_host_get_mode(u)
        except Exception as e:
            steps[-1].ok = False
            steps[-1].actual = str(e)[:200]
            shots = on_fail_shot(cid) if on_fail_shot else []
            rep.add(
                id=cid,
                title=f"Live SoftLand probe ({tag})",
                suite="softland",
                category="positive",
                ok=False,
                error_code=EC.E110,
                expected="reply",
                actual=str(e),
                steps=steps,
                screenshots=shots,
                tags=["live", tag],
            )
            continue
        if not enabled:
            ok = r.get("action") == "allow" and r.get("reason") == "softland_off"
            exp = "allow/softland_off"
        else:
            ok = r.get("action") in ("allow", "block")
            exp = "allow|block"
        steps[-1].actual = f"{r.get('action')}/{r.get('reason')}"
        steps[-1].ok = ok
        shots = on_fail_shot(cid) if (not ok and on_fail_shot) else []
        rep.add(
            id=cid,
            title=f"Live SoftLand probe ({tag})",
            suite="softland",
            category="positive",
            ok=ok,
            error_code=EC.E100 if not ok else EC.OK,
            expected=exp,
            actual={"action": r.get("action"), "reason": r.get("reason")},
            steps=steps,
            screenshots=shots,
            tags=["live", tag],
        )
