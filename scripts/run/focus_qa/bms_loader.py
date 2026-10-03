"""Load sibling block_mode_suite.py safely (Python 3.14 dataclasses need sys.modules)."""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

_BMS_PATH = Path(__file__).resolve().parent.parent / "block_mode_suite.py"
_NAME = "calt_block_mode_suite"


def load_bms():
    if _NAME in sys.modules:
        return sys.modules[_NAME]
    spec = importlib.util.spec_from_file_location(_NAME, _BMS_PATH)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load {_BMS_PATH}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[_NAME] = mod
    spec.loader.exec_module(mod)
    return mod
