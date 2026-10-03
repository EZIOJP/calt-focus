# CALT Productivity — Installer (Focus + native enforcer)

**Status:** Start Menu shortcuts + optional enforcer helpers + **bundled `calt_enforcer.exe`** when built + **option B uninstall protect**.

Until PyInstaller ships a frozen Focus EXE, this Inno Setup script:

1. Installs into `{localappdata}\CALT Productivity` (default; no admin)
2. Creates **Start Menu** (and optional Desktop) shortcuts → `run\run_calt_desktop.bat` (web Focus)
3. Optionally adds **HKCU Run** autostart
4. Optionally registers **CALT Enforcer** (Task Scheduler — **native C++ preferred**, Python fallback)
5. Optionally installs **Windows Service** via **Register Native Enforcer** (Admin UAC)
6. Copies `installer_payload\bin\calt_enforcer.exe` into `{app}\bin` when present
7. Installs **Uninstall CALT Productivity.bat** (password gate when Protect uninstall is on)

**Architecture:** Native `calt_enforcer` owns hard-block **kills** + non-browser sessions; web `/productivity/focus` is the **control UI**; SoftLand decide is native msg-host (HTTP fallback until P4).

Your **full git clone + `.venv` remain required**. The installer does not copy Python, backend code, or data.

### Enable enforcer after install (owner)

```powershell
# No admin — Task Scheduler (native if built)
powershell -ExecutionPolicy Bypass -File scripts\desktop_tracker\install\install_enforcer_service.ps1 -Start

# Admin — real Windows Service (native)
powershell -File scripts\desktop_tracker\install\install_native_enforcer.ps1
```

Or Start Menu → **Register Enforcer (Task Scheduler)** / **Register Native Enforcer (Admin Service)**.

---

## Prerequisites

| Need | Notes |
|------|--------|
| **Inno Setup 6** | Only to *compile* the `.iss`. [jrsoftware.org/isinfo.php](https://jrsoftware.org/isinfo.php) |
| **CALT repo clone** | Folder containing `scripts\` and `backend\` |
| **Python venv** | `<repo>\.venv` for API / SoftLand fallback |
| **Native enforcer** | `scripts\desktop_tracker\build\build_native_enforcer.bat` → copies exe into `installer_payload\bin\` |
| **WebView2 Runtime** | Usually already on Win10/11 |

---

## How the owner compiles

```bat
scripts\desktop_tracker\build\build_native_enforcer.bat
scripts\desktop_tracker\installer\compile_installer.bat
```

Or open `install_calt_desktop.iss` in Inno Setup Compiler → Build.

Output: `scripts\desktop_tracker\installer\Output\CALTProductivitySetup-0.2.2-protect.exe`

---

## What gets installed

```
%LOCALAPPDATA%\CALT Productivity\
  CALT Productivity.bat
  Register Enforcer.bat
  Register Native Enforcer.bat
  Uninstall CALT Productivity.bat
  bin\calt_enforcer.exe     (if bundled at compile time)
  repo_root.txt
  INSTALLER.md
```

---

## Uninstall protect (option B)

| Goal | How |
|------|-----|
| **Stop casual uninstall** | Focus → **Protect uninstall** (needs Focus unlock password/phrase) |
| **Hide Apps & features entry** | Sets ARP `SystemComponent=1` + neutralizes `UninstallString` (HKCU Inno key) |
| **Allow uninstall again** | Uncheck Protect in Focus (enter password), **or** Start Menu → **Uninstall CALT Productivity** and enter password |
| **Not CT-hard** | Admin / registry edits can still bypass — this is friction, not kernel lock |

Secrets reuse `enforcer_policy.json` `unlock_password` / `unlock_phrase` (same as Arm lock). SoftLand never Arms.

---

## Uninstall

1. Prefer Start Menu → **Uninstall CALT Productivity** (respects protect gate)
2. Or turn off Protect uninstall in Focus, then Settings → Apps → **CALT Productivity**
3. Does **not** delete the git repo, `.venv`, or SQLite data
4. Unregister enforcer manually if needed:

```powershell
Unregister-ScheduledTask -TaskName "CALT Enforcer" -Confirm:$false
powershell -File scripts\desktop_tracker\install\install_native_enforcer.ps1 -Uninstall
```
