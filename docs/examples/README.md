# Example exports & infographics

Owner-sample artifacts for the product README — not fixtures for tests.

| File | What |
|------|------|
| `august_2026_sample_export.json` | Rollup from `productivity.db` `tracked_sessions` (+ planner / rewards) for **2026-08** |
| `august_2026_day_metrics.csv` | Per-day actual / focus-ish / distract minutes |
| `infographic_*.svg` | Cute postcards embedded in the root README |

**Notes**

- Window titles were never included; categories and app-level totals in the DB stay category-first in the postcard.
- “Focus-ish” = Study / IDE / Coursework / AI·ML / coding-adjacent categories (see JSON `note`).
- Sleep bars use **October** Zepp → `calt_sync_host` mirrors; August did not have watch ingest yet.
- Regenerate from the live DB with a short Python pass if you want a newer month (same shape as Settings export).
