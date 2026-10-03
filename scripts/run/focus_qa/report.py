"""Rich JSON / Markdown / HTML report writers."""
from __future__ import annotations

import html
import json
from collections import Counter
from pathlib import Path
from typing import Any

from . import error_codes as EC
from .model import SuiteReport


def write_json(path: Path, rep: SuiteReport) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data: dict[str, Any] = {
        "started_at": rep.started_at,
        "finished_at": rep.finished_at,
        "passed": rep.passed,
        "failed": rep.failed,
        "skipped": rep.skipped,
        "gateway_ok": rep.gateway_ok,
        "restored": rep.restored,
        "notes": rep.notes,
        "artifact_dir": rep.artifact_dir,
        "results": [r.to_dict() for r in rep.results],
    }
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def write_markdown(path: Path, rep: SuiteReport) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    by_suite = Counter(r.suite for r in rep.results)
    by_cat = Counter(r.category for r in rep.results)
    by_code = Counter(r.error_code for r in rep.results if r.status == "FAIL")

    lines: list[str] = []
    lines.append("# Focus QA automation report")
    lines.append("")
    lines.append(f"**When:** {rep.started_at} → {rep.finished_at}")
    lines.append(f"**Result:** **{rep.passed} PASS / {rep.failed} FAIL / {rep.skipped} SKIP**")
    lines.append(f"**Gateway:** {'ok' if rep.gateway_ok else 'down/timeout'}")
    lines.append(f"**Snapshot restored:** {rep.restored}")
    lines.append(f"**Artifacts:** `{rep.artifact_dir}`")
    lines.append("")
    lines.append("## Summary by suite")
    lines.append("")
    lines.append("| Suite | Cases |")
    lines.append("|-------|------:|")
    for k, v in sorted(by_suite.items()):
        lines.append(f"| {k} | {v} |")
    lines.append("")
    lines.append("## Summary by category")
    lines.append("")
    lines.append("| Category | Cases |")
    lines.append("|----------|------:|")
    for k, v in sorted(by_cat.items()):
        lines.append(f"| {k} | {v} |")
    lines.append("")
    if by_code:
        lines.append("## Failures by error code")
        lines.append("")
        lines.append("| Code | Meaning | Count |")
        lines.append("|------|---------|------:|")
        for code, n in sorted(by_code.items()):
            meaning = EC.DESCRIPTIONS.get(code, "")
            lines.append(f"| `{code}` | {meaning} | {n} |")
        lines.append("")

    if rep.notes:
        lines.append("## Notes")
        lines.append("")
        for n in rep.notes:
            lines.append(f"- {n}")
        lines.append("")

    lines.append("## How to run")
    lines.append("")
    lines.append("```bat")
    lines.append("python scripts\\desktop_tracker\\run\\focus_qa_suite.py")
    lines.append("python scripts\\desktop_tracker\\run\\focus_qa_suite.py --live --arm-kills --ui")
    lines.append("```")
    lines.append("")

    # Failures first
    fails = [r for r in rep.results if r.status == "FAIL"]
    if fails:
        lines.append("## Failed cases (detail)")
        lines.append("")
        for r in fails:
            _append_case_md(lines, r)
    else:
        lines.append("## Failed cases")
        lines.append("")
        lines.append("_None._")
        lines.append("")

    lines.append("## All cases")
    lines.append("")
    lines.append("| Status | Code | Suite | Cat | ID | Title |")
    lines.append("|--------|------|-------|-----|----|-------|")
    for r in rep.results:
        title = r.title.replace("|", "/")
        lines.append(
            f"| {r.status} | `{r.error_code}` | {r.suite} | {r.category} | `{r.id}` | {title} |"
        )
    lines.append("")

    lines.append("## Case details")
    lines.append("")
    for r in rep.results:
        _append_case_md(lines, r)

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _append_case_md(lines: list[str], r) -> None:
    lines.append(f"### `{r.id}` — {r.title}")
    lines.append("")
    lines.append(f"- **Status:** {r.status}")
    lines.append(f"- **Suite / category:** {r.suite} / {r.category}")
    lines.append(f"- **Error code:** `{r.error_code}` — {EC.DESCRIPTIONS.get(r.error_code, '')}")
    if r.tags:
        lines.append(f"- **Tags:** {', '.join(r.tags)}")
    if r.detail:
        lines.append(f"- **Detail:** {r.detail}")
    lines.append(f"- **Expected:** `{json.dumps(r.expected, ensure_ascii=False)[:500]}`")
    lines.append(f"- **Actual:** `{json.dumps(r.actual, ensure_ascii=False)[:500]}`")
    if r.steps:
        lines.append("")
        lines.append("| # | Step | Expected | Actual | OK |")
        lines.append("|---|------|----------|--------|----|")
        for s in r.steps:
            lines.append(
                f"| {s.n} | {s.action} | {s.expected} | {s.actual} | {'Y' if s.ok else 'N'} |"
            )
    if r.screenshots:
        lines.append("")
        lines.append("**Screenshots:**")
        for p in r.screenshots:
            # Prefer relative link if under exports
            lines.append(f"- `{p}`")
            # Markdown image if png
            if str(p).lower().endswith(".png"):
                lines.append("")
                lines.append(f"![{r.id}]({_rel_img(p)})")
    lines.append("")


def _rel_img(path_hint: str) -> str:
    """Best-effort relative path from exports/ to artifact png."""
    p = Path(path_hint)
    parts = p.parts
    if "focus-qa-artifacts" in parts:
        i = parts.index("focus-qa-artifacts")
        return "/".join(parts[i:])
    return p.name.replace("\\", "/")


def write_html(path: Path, rep: SuiteReport) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for r in rep.results:
        color = {"PASS": "#0a7", "FAIL": "#c22", "SKIP": "#888"}.get(r.status, "#000")
        shots = ""
        for s in r.screenshots:
            href = html.escape(_rel_img(s))
            shots += f'<div><a href="{href}"><img src="{href}" style="max-width:320px;border:1px solid #ccc"/></a></div>'
        step_html = ""
        if r.steps:
            step_html = "<ol>" + "".join(
                f"<li><b>{html.escape(s.action)}</b> — exp={html.escape(s.expected)} act={html.escape(s.actual)} "
                f"[{'ok' if s.ok else 'FAIL'}]</li>"
                for s in r.steps
            ) + "</ol>"
        rows.append(
            f"<tr style='vertical-align:top'>"
            f"<td style='color:{color};font-weight:700'>{r.status}</td>"
            f"<td><code>{html.escape(r.error_code)}</code></td>"
            f"<td>{html.escape(r.suite)}<br/><small>{html.escape(r.category)}</small></td>"
            f"<td><code>{html.escape(r.id)}</code><br/><b>{html.escape(r.title)}</b>"
            f"{step_html}"
            f"<div><small>exp={html.escape(str(r.expected)[:300])}</small></div>"
            f"<div><small>act={html.escape(str(r.actual)[:300])}</small></div>"
            f"{shots}</td>"
            f"</tr>"
        )
    body = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"/><title>Focus QA Report</title>
<style>
body{{font-family:Segoe UI,system-ui,sans-serif;margin:24px;background:#fafafa;color:#111}}
table{{border-collapse:collapse;width:100%;background:#fff}}
th,td{{border:1px solid #ddd;padding:8px;text-align:left}}
th{{background:#f0f0f0}}
.summary{{display:flex;gap:16px;margin-bottom:16px}}
.card{{background:#fff;border:1px solid #ddd;padding:12px 16px;border-radius:6px}}
</style></head><body>
<h1>Focus QA automation report</h1>
<div class="summary">
  <div class="card"><b>{rep.passed}</b> PASS</div>
  <div class="card"><b>{rep.failed}</b> FAIL</div>
  <div class="card"><b>{rep.skipped}</b> SKIP</div>
  <div class="card">Gateway: {"ok" if rep.gateway_ok else "down"}</div>
  <div class="card">Restored: {rep.restored}</div>
</div>
<p>{html.escape(rep.started_at)} → {html.escape(rep.finished_at)}</p>
<table>
<thead><tr><th>Status</th><th>Code</th><th>Suite</th><th>Case / steps / screenshots</th></tr></thead>
<tbody>
{''.join(rows)}
</tbody></table>
</body></html>"""
    path.write_text(body, encoding="utf-8")
