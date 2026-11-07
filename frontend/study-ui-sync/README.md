# Study UI sync — enforcer Settings cleanup

React SoT is the Study sibling (`cognitive-aware-learning-tutor` `src/`).
This cloud agent cannot push to that repo, so the Focus enforcer Settings
cleanup lands here for you to apply locally, then `npm run build:focus`.

## Apply (pick one)

### A) Patch (preferred)

From the Study repo root (sibling of this tree):

```bat
git apply --verbose ..\calt-focus\patches\0001-consolidate-focus-enforcer-settings-onto-settings-pages.patch
npm run build:focus
```

Or on Unix:

```bash
git apply --verbose ../calt-focus/patches/0001-consolidate-focus-enforcer-settings-onto-settings-pages.patch
npm run build:focus
```

Then sync: `npm run sync:focus-ui` from calt-focus (or the build script already syncs).

### B) Copy these six files

Copy `frontend/study-ui-sync/src/components/productivity/*.tsx` over the same
paths under Study `src/components/productivity/`.

## What changed

- Home: SoftLand list **status** only → Settings → Blocks → Sites
- Plan → Goals: remove Blocker editors → deep links to Sites / Apps
- Unlock: Focus hides Study scoring exe chips; kill-list links → `section=apps`
- Productive: hide Session override + ClassificationReview in Focus shell
- **Does not** activate SoftLand or Arm

See `docs/SETTINGS.md` §5.3.
