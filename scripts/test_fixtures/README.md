# Arm kill-test fixtures

Disposable console processes for live Arm tests. **Never** put real games in the suite kill list.

| Binary | Role |
|--------|------|
| `calt_test_block_target.exe` | Temporarily added to Arm `exes` — must be killed when Armed |
| `calt_test_safe.exe` | Must **never** be on the kill list — misfire canary |

## Rebuild

Requires `clang` (LLVM/MinGW) on PATH:

```bat
scripts\desktop_tracker\test_fixtures\build_fixtures.bat
```

Sources: `calt_test_block_target.c`, `calt_test_safe.c` (sleep ~120s then exit).

## Run with suite

```bat
python scripts\desktop_tracker\run\block_mode_suite.py --live --arm-kills
```

The suite snapshots SoftLand/Arm, arms **only** the block-target name, asserts kill/safe, disarms, restores.
