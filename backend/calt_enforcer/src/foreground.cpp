#include "foreground.h"

#include <windows.h>

#include <string>

namespace {

std::wstring ToLower(std::wstring s) {
  for (auto& c : s) c = (wchar_t)towlower(c);
  return s;
}

std::wstring BaseName(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

}  // namespace

bool GetForegroundInfo(ForegroundInfo& out) {
  out = ForegroundInfo{};

  HWND hwnd = GetForegroundWindow();
  if (!hwnd) return false;

  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!pid) return false;

  wchar_t title[512];
  title[0] = 0;
  GetWindowTextW(hwnd, title, 512);

  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return false;

  wchar_t path[MAX_PATH];
  DWORD n = MAX_PATH;
  bool ok = false;
  if (QueryFullProcessImageNameW(h, 0, path, &n) && n > 0) {
    out.exe = ToLower(BaseName(path));
    out.title = title;
    if (out.title.size() > 200) out.title.resize(200);
    out.pid = pid;
    out.ok = !out.exe.empty();
    ok = out.ok;
  }
  CloseHandle(h);
  return ok;
}

ULONGLONG GetInteractiveIdleMs() {
  LASTINPUTINFO lii{};
  lii.cbSize = sizeof(lii);
  if (!GetLastInputInfo(&lii)) return 0;
  DWORD tick = GetTickCount();
  return (ULONGLONG)(tick - lii.dwTime);
}
