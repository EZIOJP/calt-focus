# Focus frontend

**Primary UI:** [`shell/`](shell/) — plain HTML/CSS/JS (hybrid design). No Study `src/`, no React required.

## Edit screens

There is no visual screen builder. Edit source under `shell/`, ship to `dist-focus/`, reload Focus.

| Change | File |
|--------|------|
| Nav / chrome | `shell/index.html`, `shell/css/shell.css` |
| Colors / type | `shell/css/tokens.css` |
| Home | `shell/js/pages/home.js` |
| Morning gate (bible → plan → Confirm) | `shell/js/pages/morning.js` |
| Plan quick-add / Confirm | `shell/js/pages/plan.js` |
| Bible mark-done | `shell/js/pages/bible.js` |
| Settings (all rows, one tab) | `shell/js/pages/settings.js` |
| Calendar / Journal | `shell/js/pages/*.js` |
| Gate helpers | `shell/js/day-loop.js` |
| Enforcer pipe | `shell/js/bridge.js` |
| JSON mirrors | `shell/js/mirrors.js` |
| Routes | `shell/js/router.js` |

### Loop

```bat
:: 1) edit frontend\shell\...
npm run build:focus
:: 2) Focus tray → Reload UI  (or restart calt_focus.exe)
```

`npm run build:focus` copies `frontend/shell/` → `dist-focus/` (what WebView2 loads as `https://calt.app`).

`scripts\run\run_calt_desktop.bat` runs the same ship step if `dist-focus\` is missing.

Do **not** reintroduce Study monorepo React aliases into this repo.
