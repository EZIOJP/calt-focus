"""Focus QA result model — cases with steps, error codes, screenshots."""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from typing import Any


def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


@dataclass
class StepResult:
    n: int
    action: str
    expected: str = ""
    actual: str = ""
    ok: bool = True


@dataclass
class CaseResult:
    id: str
    title: str
    suite: str  # softland | arm | gateway | ui | sync
    category: str  # positive | edge | negative
    ok: bool
    status: str  # PASS | FAIL | SKIP
    error_code: str = "OK"
    expected: Any = None
    actual: Any = None
    detail: str = ""
    steps: list[StepResult] = field(default_factory=list)
    screenshots: list[str] = field(default_factory=list)
    tags: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        return d


@dataclass
class SuiteReport:
    started_at: str
    finished_at: str = ""
    results: list[CaseResult] = field(default_factory=list)
    restored: bool = False
    gateway_ok: bool = False
    notes: list[str] = field(default_factory=list)
    artifact_dir: str = ""

    @property
    def passed(self) -> int:
        return sum(1 for r in self.results if r.status == "PASS")

    @property
    def failed(self) -> int:
        return sum(1 for r in self.results if r.status == "FAIL")

    @property
    def skipped(self) -> int:
        return sum(1 for r in self.results if r.status == "SKIP")

    def add(
        self,
        *,
        id: str,
        title: str,
        suite: str,
        category: str,
        ok: bool,
        error_code: str = "OK",
        expected: Any = None,
        actual: Any = None,
        detail: str = "",
        steps: list[StepResult] | None = None,
        screenshots: list[str] | None = None,
        tags: list[str] | None = None,
        skipped: bool = False,
    ) -> CaseResult:
        if skipped:
            status = "SKIP"
            ok = True
        else:
            status = "PASS" if ok else "FAIL"
            if ok:
                error_code = "OK"
        c = CaseResult(
            id=id,
            title=title,
            suite=suite,
            category=category,
            ok=ok if not skipped else True,
            status=status,
            error_code=error_code if status == "FAIL" else ("OK" if status == "PASS" else "OK"),
            expected=expected,
            actual=actual,
            detail=detail,
            steps=steps or [],
            screenshots=screenshots or [],
            tags=tags or [],
        )
        if status == "SKIP":
            c.error_code = "OK"
        self.results.append(c)
        return c
