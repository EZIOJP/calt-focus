#include "kill.h"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

namespace {

std::wstring ToLower(std::wstring s) {
  for (auto& c : s) c = (wchar_t)towlower(c);
  return s;
}

std::wstring BaseNameLower(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
  return ToLower(name);
}

// Normalize policy entry: path → basename, lower, ensure comparable.
std::wstring NormalizeExeToken(const std::wstring& raw) {
  std::wstring b = BaseNameLower(raw);
  while (!b.empty() && (b.front() == L' ' || b.front() == L'"')) b.erase(b.begin());
  while (!b.empty() && (b.back() == L' ' || b.back() == L'"')) b.pop_back();
  return b;
}

bool ExeTokensMatch(const std::wstring& processBase, const std::wstring& token) {
  if (processBase.empty() || token.empty()) return false;
  if (processBase == token) return true;
  // "steam" matches "steam.exe"
  if (token.find(L'.') == std::wstring::npos && processBase == token + L".exe") return true;
  if (processBase.find(L'.') == std::wstring::npos && token == processBase + L".exe") return true;
  return false;
}

// CALT decision (logic gap L2): protect OS + this service + Python API host.
// Do NOT blanket-protect browsers — if user listed them, kill (CT-like).
bool IsProtected(const std::wstring& base) {
  static const wchar_t* kProtExact[] = {
      L"explorer.exe", L"csrss.exe",     L"winlogon.exe", L"services.exe",
      L"lsass.exe",    L"svchost.exe",   L"smss.exe",     L"fontdrvhost.exe",
      L"dwm.exe",      L"calt_enforcer.exe", L"calt_focus.exe", L"calt_msg_host.exe",
      nullptr};
  for (int i = 0; kProtExact[i]; ++i) {
    if (base == kProtExact[i]) return true;
  }
  // Keep FastAPI / Desktop Python alive even if mis-listed.
  if (base == L"python.exe" || base == L"pythonw.exe") return true;
  return false;
}

void AppendKillLog(const std::wstring& exe, DWORD pid) {
  wchar_t* env = _wgetenv(L"CALT_DB");
  std::wstring logPath;
  if (env && *env) {
    std::wstring db(env);
    size_t slash = db.find_last_of(L"\\/");
    std::wstring dataDir = (slash == std::wstring::npos) ? L"." : db.substr(0, slash);
    logPath = dataDir + L"\\behavior\\enforcer_kills.log";
  } else {
    logPath = L"enforcer_kills.log";
  }
  // Ensure parent
  size_t slash = logPath.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    CreateDirectoryW(logPath.substr(0, slash).c_str(), nullptr);
  }
  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, logPath.c_str(), L"ab");
#else
  f = _wfopen(logPath.c_str(), L"ab");
#endif
  if (!f) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u kill pid=%lu exe=", st.wYear, st.wMonth, st.wDay,
          st.wHour, st.wMinute, st.wSecond, (unsigned long)pid);
  // exe as UTF-8-ish narrow best-effort
  for (wchar_t c : exe) {
    if (c < 128) fputc((char)c, f);
  }
  fputc('\n', f);
  fclose(f);
}

// Tray balloon even when Focus is not running (hidden notify icon).
void ShowKillToast(const std::wstring& exe) {
  static HWND hwnd = nullptr;
  static NOTIFYICONDATAW nid{};
  static bool added = false;
  if (!hwnd) {
    hwnd = CreateWindowExW(0, L"STATIC", L"CALTEnforcerToast", 0, 0, 0, 0, 0, HWND_MESSAGE,
                           nullptr, GetModuleHandleW(nullptr), nullptr);
  }
  if (!hwnd) return;
  if (!added) {
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_APP + 40;
    nid.hIcon = LoadIconW(nullptr, IDI_WARNING);
    wcsncpy_s(nid.szTip, L"CALT Enforcer", _TRUNCATE);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) added = true;
  }
  if (!added) return;
  nid.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
  nid.dwInfoFlags = NIIF_WARNING;
  wcsncpy_s(nid.szInfoTitle, L"CALT blocked an app", _TRUNCATE);
  std::wstring body = L"Closed: " + exe + L" — hard block is armed.";
  wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

}  // namespace

KillResult KillMatchingExes(const std::vector<std::wstring>& exes) {
  KillResult result;
  if (exes.empty()) return result;

  std::vector<std::wstring> tokens;
  tokens.reserve(exes.size());
  for (const auto& e : exes) {
    std::wstring t = NormalizeExeToken(e);
    if (!t.empty()) tokens.push_back(t);
  }
  if (tokens.empty()) return result;

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return result;

  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snap, &pe)) {
    do {
      std::wstring base = BaseNameLower(pe.szExeFile);
      if (IsProtected(base)) continue;
      bool match = false;
      for (const auto& t : tokens) {
        if (ExeTokensMatch(base, t)) {
          match = true;
          break;
        }
      }
      if (!match) continue;
      HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                             pe.th32ProcessID);
      if (!h) continue;
      if (TerminateProcess(h, 1)) {
        ++result.killed;
        result.last_exe = base;
        result.last_pid = pe.th32ProcessID;
        AppendKillLog(base, pe.th32ProcessID);
        ShowKillToast(base);
      }
      CloseHandle(h);
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return result;
}
