#pragma once
#include <windows.h>
#include <string>

struct ForegroundInfo {
  std::wstring exe;    // basename lower, e.g. notepad.exe
  std::wstring title;  // window title (truncated)
  DWORD pid = 0;
  bool ok = false;
};

// Current foreground window process + title. ok=false if desktop/idle.
// Safe under LocalSystem (Session 0): attaches to the console user's input desktop.
bool GetForegroundInfo(ForegroundInfo& out);

// Idle milliseconds for the interactive console session (not Session 0).
// Under a service, raw GetLastInputInfo is Session-0-local and looks "always idle".
ULONGLONG GetInteractiveIdleMs();
