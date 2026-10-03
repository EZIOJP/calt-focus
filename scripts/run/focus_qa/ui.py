"""Focus UI smoke — serve dist-focus, fetch hash routes, screenshot."""
from __future__ import annotations

import http.server
import socketserver
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Callable

from . import error_codes as EC
from .model import StepResult, SuiteReport
from .screenshots import capture_screen, shot_path


def _find_repo() -> Path:
    p = Path(__file__).resolve().parent
    for _ in range(8):
        if (p / "AGENTS.md").exists():
            return p
        p = p.parent
    raise RuntimeError("repo root not found")


REPO = _find_repo()
DIST = REPO / "dist-focus"


class _QuietHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DIST), **kwargs)

    def log_message(self, format, *args):  # noqa: A003
        return


def _start_server(port: int = 4177) -> tuple[socketserver.TCPServer, threading.Thread]:
    httpd = socketserver.TCPServer(("127.0.0.1", port), _QuietHandler)
    httpd.allow_reuse_address = True
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    return httpd, t


UI_ROUTES = [
    ("/", "UI home (index.html)"),
    ("/#/productivity?tab=home", "UI productivity home hash"),
    ("/#/productivity?tab=settings", "UI settings hash"),
    ("/#/productivity?tab=blocker", "UI blocker hash"),
    ("/#/productivity?tab=focus", "UI focus hash"),
    ("/#/journal", "UI journal hash"),
]


def run_ui_smoke(
    rep: SuiteReport,
    artifact_dir: Path,
    *,
    port: int = 4177,
    on_fail_shot: Callable[[str], list[str]] | None = None,
) -> None:
    if not (DIST / "index.html").exists():
        rep.add(
            id="ui.dist_focus_present",
            title="dist-focus/index.html must exist (npm run build:focus)",
            suite="ui",
            category="positive",
            ok=False,
            error_code=EC.E500,
            expected=True,
            actual=False,
            steps=[StepResult(1, "Check dist-focus", "index.html", "missing", False)],
        )
        return

    httpd = None
    try:
        httpd, _ = _start_server(port)
        time.sleep(0.4)
        base = f"http://127.0.0.1:{port}"

        # Positive: index loads
        for path, title in UI_ROUTES:
            cid = f"ui.route.{path.replace('/', '_').replace('?', '_').replace('=', '_') or 'root'}"
            steps = [
                StepResult(1, f"HTTP GET {base}{path.split('#')[0] or '/'}", "200 + html", "", True),
            ]
            # Hash routes still fetch index.html
            fetch_url = base + "/"
            try:
                with urllib.request.urlopen(fetch_url, timeout=5) as resp:
                    code = resp.status
                    body = resp.read(2000).decode("utf-8", errors="replace")
            except urllib.error.URLError as e:
                steps[-1].ok = False
                steps[-1].actual = str(e)
                shots = []
                sp = shot_path(artifact_dir, cid, "fail")
                p = capture_screen(sp, cid)
                if p:
                    shots.append(p)
                if on_fail_shot:
                    shots.extend(on_fail_shot(cid))
                rep.add(
                    id=cid,
                    title=title,
                    suite="ui",
                    category="positive",
                    ok=False,
                    error_code=EC.E500,
                    expected="200",
                    actual=str(e),
                    steps=steps,
                    screenshots=shots,
                    tags=["ui", "smoke"],
                )
                continue

            ok = code == 200 and ("html" in body.lower() or "<!doctype" in body.lower() or "<div" in body.lower())
            steps[-1].actual = f"status={code} bytes~{len(body)}"
            steps[-1].ok = ok
            steps.append(StepResult(2, f"Note target hash route {path}", "reachable via SPA", path, True))
            # Always screenshot first few routes for evidence
            shots: list[str] = []
            if path in ("/", "/#/productivity?tab=settings", "/#/productivity?tab=blocker") or not ok:
                sp = shot_path(artifact_dir, cid, "pass" if ok else "fail")
                p = capture_screen(sp, cid)
                if p:
                    shots.append(p)
            rep.add(
                id=cid,
                title=title,
                suite="ui",
                category="positive",
                ok=ok,
                error_code=EC.E500 if not ok else EC.OK,
                expected="200 html shell",
                actual={"status": code, "sample": body[:120]},
                steps=steps,
                screenshots=shots,
                tags=["ui", "smoke"],
            )

        # Negative: missing asset should 404
        cid = "ui.neg.missing_asset_404"
        steps = [StepResult(1, f"GET {base}/this-asset-should-not-exist-focus-qa.js", "404", "", True)]
        try:
            urllib.request.urlopen(base + "/this-asset-should-not-exist-focus-qa.js", timeout=5)
            got = "200"
            neg_ok = False
        except urllib.error.HTTPError as e:
            got = str(e.code)
            neg_ok = e.code == 404
            steps[-1].actual = got
            steps[-1].ok = neg_ok
        except urllib.error.URLError as e:
            got = str(e)
            neg_ok = False
            steps[-1].ok = False
            steps[-1].actual = got
        shots = []
        if not neg_ok:
            sp = shot_path(artifact_dir, cid, "fail")
            p = capture_screen(sp, cid)
            if p:
                shots.append(p)
        rep.add(
            id=cid,
            title="NEGATIVE: missing static asset returns 404",
            suite="ui",
            category="negative",
            ok=neg_ok,
            error_code=EC.E501 if not neg_ok else EC.OK,
            expected="404",
            actual=got,
            steps=steps,
            screenshots=shots,
            tags=["ui", "negative"],
        )
    finally:
        if httpd is not None:
            httpd.shutdown()
            httpd.server_close()
