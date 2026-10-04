# Focus frontend

**UI:** React Focus SPA, built in the Study sibling and synced to `dist-focus/` (what WebView2 loads as `https://calt.app`).

There is no in-repo HTML shell.

## Loop

```bat
:: From calt-focus root (Study sibling must exist):
npm run build:focus
:: or, if Study already has dist-focus:
npm run sync:focus-ui

:: Then Focus tray → Reload UI
```

`scripts\run\run_calt_desktop.bat` syncs Study `dist-focus` if local `dist-focus\` is missing.

SoftLand / Arm stay native (enforcer + msg-host). No Study `:8000` SoftLand brain.
