#include "focus_watchdog.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::wstring DirOf(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::wstring SoftlandPolicyPath(const std::wstring& dbPath) {
  return Join(DirOf(dbPath), L"behavior\\softland_policy.json");
}

bool JsonBoolNear(const std::string& body, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) ++i;
  if (body.compare(i, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (body.compare(i, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

std::string UtcIsoNow() {
  SYSTEMTIME st;
  GetSystemTime(&st);
  char iso[40];
  snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return iso;
}

std::wstring ExeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (!n || n >= MAX_PATH) return L".";
  return DirOf(buf);
}

}  // namespace

bool ReadSoftlandEnabled(const std::wstring& dbPath, bool* out_enabled) {
  if (!out_enabled) return false;
  *out_enabled = false;
  const std::wstring path = SoftlandPolicyPath(dbPath);
  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, path.c_str(), L"rb");
#else
  f = _wfopen(path.c_str(), L"rb");
#endif
  if (!f) return false;
  std::string body;
  char buf[4096];
  while (size_t n = fread(buf, 1, sizeof(buf), f)) body.append(buf, n);
  fclose(f);
  bool v = false;
  if (JsonBoolNear(body, "softland_enabled", &v)) {
    *out_enabled = v;
    return true;
  }
  return false;
}

bool ProcessBasenameRunning(const wchar_t* exeBase) {
  if (!exeBase || !*exeBase) return false;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  bool found = false;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, exeBase) == 0) {
        found = true;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return found;
}

std::wstring ResolveFocusExePath() {
  wchar_t* envFocus = _wgetenv(L"CALT_FOCUS_EXE");
  if (envFocus && *envFocus && GetFileAttributesW(envFocus) != INVALID_FILE_ATTRIBUTES) {
    return envFocus;
  }
  const std::wstring sibling = Join(ExeDir(), L"calt_focus.exe");
  if (GetFileAttributesW(sibling.c_str()) != INVALID_FILE_ATTRIBUTES) return sibling;

  wchar_t* repo = _wgetenv(L"CALT_REPO");
  if (repo && *repo) {
    const std::wstring cands[] = {
        Join(repo, L"calt-focus\\backend\\calt_focus\\build\\Release\\calt_focus.exe"),
        Join(repo, L"calt-focus\\backend\\calt_focus\\build\\calt_focus.exe"),
        Join(repo, L"native\\calt_focus\\build\\Release\\calt_focus.exe"),
        Join(repo, L"native\\calt_focus\\build\\calt_focus.exe"),
    };
    for (const auto& c : cands) {
      if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) return c;
    }
  }
  // Walk up from enforcer build dir → repo
  std::wstring dir = ExeDir();
  for (int i = 0; i < 8; ++i) {
    const std::wstring c1 =
        Join(dir, L"calt-focus\\backend\\calt_focus\\build\\Release\\calt_focus.exe");
    const std::wstring c2 = Join(dir, L"calt-focus\\backend\\calt_focus\\build\\calt_focus.exe");
    if (GetFileAttributesW(c1.c_str()) != INVALID_FILE_ATTRIBUTES) return c1;
    if (GetFileAttributesW(c2.c_str()) != INVALID_FILE_ATTRIBUTES) return c2;
    dir = DirOf(dir);
  }
  return L"";
}

void TickFocusWatchdog(bool armed, bool softland_enabled, FocusWatchState& state) {
  state.softland_enabled = softland_enabled;
  state.softland_or_armed = softland_enabled || armed;
  state.focus_running = ProcessBasenameRunning(L"calt_focus.exe");

  static DWORD window_start_ms = 0;
  static unsigned launches_in_window = 0;
  static unsigned lifetime_relaunches = 0;
  static std::string last_relaunch_iso;
  static bool suppressed = false;

  const DWORD now = GetTickCount();
  if (window_start_ms == 0 || now - window_start_ms > 60000) {
    window_start_ms = now;
    launches_in_window = 0;
    suppressed = false;
  }

  state.focus_relaunch_count = lifetime_relaunches;
  state.focus_last_relaunch_at = last_relaunch_iso;
  state.focus_relaunch_suppressed = suppressed;

  if (!state.softland_or_armed) {
    suppressed = false;
    state.focus_relaunch_suppressed = false;
    return;
  }
  if (state.focus_running) return;
  if (launches_in_window >= 3) {
    suppressed = true;
    state.focus_relaunch_suppressed = true;
    return;
  }

  const std::wstring exe = ResolveFocusExePath();
  if (exe.empty()) {
    suppressed = true;
    state.focus_relaunch_suppressed = true;
    return;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, DirOf(exe).c_str(),
                     &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    ++launches_in_window;
    ++lifetime_relaunches;
    last_relaunch_iso = UtcIsoNow();
    suppressed = false;
    state.focus_relaunch_count = lifetime_relaunches;
    state.focus_last_relaunch_at = last_relaunch_iso;
    state.focus_running = true;
    state.focus_relaunch_suppressed = false;
  }
}
