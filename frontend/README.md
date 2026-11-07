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

**Settings ownership:** SoftLand site lists, Arm kill list, device lock, schedules, and
mode flags are edited only under Focus **Settings → Blocks / Unlock**. Home and Plan
show status + deep links — they do not host duplicate list editors. Do not activate /
Arm the enforcer as part of UI cleanup.
