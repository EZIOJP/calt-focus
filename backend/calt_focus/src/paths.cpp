#include "paths.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <vector>

namespace {

std::wstring TrimSlash(std::wstring p) {
  while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) {
    p.pop_back();
  }
  return p;
}

std::wstring Join(std::wstring a, const std::wstring& b) {
  a = TrimSlash(std::move(a));
  if (!b.empty() && (b[0] == L'\\' || b[0] == L'/')) {
    return a + b;
  }
  return a + L"\\" + b;
}

bool DirExists(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

std::wstring ExeDir() {
  wchar_t buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return L".";
  }
  PathRemoveFileSpecW(buf);
  return buf;
}

std::wstring RepoRoot() {
  // Prefer CALT_REPO, else walk up from exe looking for data/ + scripts/ (Focus repo root)
  if (const wchar_t* env = _wgetenv(L"CALT_REPO")) {
    if (env[0]) {
      return TrimSlash(env);
    }
  }

  std::wstring cur = ExeDir();
  for (int i = 0; i < 8; ++i) {
    if (DirExists(Join(cur, L"data")) && DirExists(Join(cur, L"scripts"))) {
      return cur;
    }
    // build/Release or build → native/calt_focus → native → repo
    const size_t slash = cur.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
      break;
    }
    cur = cur.substr(0, slash);
  }
  return ExeDir();
}

std::wstring DataBehaviorDir() {
  if (const wchar_t* env = _wgetenv(L"CALT_BEHAVIOR_DIR")) {
    if (env[0]) return env;
  }
  if (const wchar_t* env = _wgetenv(L"CALT_DB")) {
    if (env[0]) {
      std::wstring db = env;
      const size_t slash = db.find_last_of(L"\\/");
      if (slash != std::wstring::npos) {
        return Join(db.substr(0, slash), L"behavior");
      }
    }
  }
  const std::wstring prod = Join(RepoRoot(), L"data\\productivity\\behavior");
  if (GetFileAttributesW(prod.c_str()) != INVALID_FILE_ATTRIBUTES) return prod;
  return Join(RepoRoot(), L"data\\behavior");  // legacy
}

std::wstring DataBibleDir() {
  if (const wchar_t* env = _wgetenv(L"CALT_BIBLE_DIR")) {
    if (env[0]) return env;
  }
  if (const wchar_t* env = _wgetenv(L"CALT_DB")) {
    if (env[0]) {
      std::wstring db = env;
      const size_t slash = db.find_last_of(L"\\/");
      if (slash != std::wstring::npos) {
        return Join(db.substr(0, slash), L"bible");
      }
    }
  }
  const std::wstring prod = Join(RepoRoot(), L"data\\productivity\\bible");
  if (GetFileAttributesW(prod.c_str()) != INVALID_FILE_ATTRIBUTES) return prod;
  return Join(RepoRoot(), L"data\\bible");  // legacy
}

std::wstring EnforcerStatusPath() {
  return Join(DataBehaviorDir(), L"enforcer_status.json");
}

std::wstring SoftlandPolicyPath() {
  return Join(DataBehaviorDir(), L"softland_policy.json");
}

std::wstring DistDir() {
  const std::wstring focusDist = Join(RepoRoot(), L"dist-focus");
  const std::wstring focusIndex = Join(focusDist, L"index.html");
  const DWORD a = GetFileAttributesW(focusIndex.c_str());
  if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
    return focusDist;
  }
  return Join(RepoRoot(), L"dist");
}

bool HasPrebuiltWebUi() {
  const std::wstring index = Join(DistDir(), L"index.html");
  const DWORD a = GetFileAttributesW(index.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FileUrlFromPath(const std::wstring& absPath) {
  std::wstring p = absPath;
  for (auto& ch : p) {
    if (ch == L'\\') {
      ch = L'/';
    }
  }
  // Encode spaces (common on Desktop paths) — still prefer https://calt.app over file://
  std::wstring out;
  out.reserve(p.size() + 16);
  for (wchar_t ch : p) {
    if (ch == L' ') {
      out += L"%20";
    } else {
      out.push_back(ch);
    }
  }
  return L"file:///" + out;
}

std::wstring FocusUrl() {
  if (const wchar_t* env = _wgetenv(L"CALT_FOCUS_URL")) {
    if (env[0]) {
      return env;
    }
  }
  // Prefer virtual-host HTTPS (ES modules work). file:// blanks WebView2 (module CORS).
  if (HasPrebuiltWebUi()) {
    if (const wchar_t* mode = _wgetenv(L"CALT_FOCUS_UI_MODE")) {
      if (_wcsicmp(mode, L"file") == 0) {
        return FileUrlFromPath(Join(DistDir(), L"index.html")) + L"#/productivity/focus";
      }
      if (_wcsicmp(mode, L"static") == 0) {
        return L"http://127.0.0.1:5174/index.html#/productivity/focus";
      }
    }
    return L"https://calt.app/index.html#/productivity/focus";
  }
  return L"http://127.0.0.1:5173/productivity/focus";
}

std::wstring ProductivityUrl() {
  if (const wchar_t* env = _wgetenv(L"CALT_PRODUCTIVITY_URL")) {
    if (env[0]) {
      return env;
    }
  }
  if (HasPrebuiltWebUi()) {
    if (const wchar_t* mode = _wgetenv(L"CALT_FOCUS_UI_MODE")) {
      if (_wcsicmp(mode, L"file") == 0) {
        return FileUrlFromPath(Join(DistDir(), L"index.html")) + L"#/productivity?tab=home";
      }
      if (_wcsicmp(mode, L"static") == 0) {
        return L"http://127.0.0.1:5174/index.html#/productivity?tab=home";
      }
    }
    return L"https://calt.app/index.html#/productivity?tab=home";
  }
  return L"http://127.0.0.1:5173/productivity?tab=home";
}

std::wstring SettingsUrl() {
  if (const wchar_t* env = _wgetenv(L"CALT_SETTINGS_URL")) {
    if (env[0]) {
      return env;
    }
  }
  // HashRouter: tab=settings opens Productivity Settings hub (Focus/SoftLand at top).
  if (HasPrebuiltWebUi()) {
    if (const wchar_t* mode = _wgetenv(L"CALT_FOCUS_UI_MODE")) {
      if (_wcsicmp(mode, L"file") == 0) {
        return FileUrlFromPath(Join(DistDir(), L"index.html")) +
               L"#/productivity?tab=settings";
      }
      if (_wcsicmp(mode, L"static") == 0) {
        return L"http://127.0.0.1:5174/index.html#/productivity?tab=settings";
      }
    }
    return L"https://calt.app/index.html#/productivity?tab=settings";
  }
  return L"http://127.0.0.1:5173/productivity?tab=settings";
}

std::wstring CalendarUrl() {
  if (const wchar_t* env = _wgetenv(L"CALT_CALENDAR_URL")) {
    if (env[0]) {
      return env;
    }
  }
  if (HasPrebuiltWebUi()) {
    if (const wchar_t* mode = _wgetenv(L"CALT_FOCUS_UI_MODE")) {
      if (_wcsicmp(mode, L"file") == 0) {
        return FileUrlFromPath(Join(DistDir(), L"index.html")) +
               L"#/productivity?tab=calendar";
      }
      if (_wcsicmp(mode, L"static") == 0) {
        return L"http://127.0.0.1:5174/index.html#/productivity?tab=calendar";
      }
    }
    return L"https://calt.app/index.html#/productivity?tab=calendar";
  }
  return L"http://127.0.0.1:5173/productivity?tab=calendar";
}

std::wstring PlanUrl() {
  if (const wchar_t* env = _wgetenv(L"CALT_PLAN_URL")) {
    if (env[0]) {
      return env;
    }
  }
  if (HasPrebuiltWebUi()) {
    if (const wchar_t* mode = _wgetenv(L"CALT_FOCUS_UI_MODE")) {
      if (_wcsicmp(mode, L"file") == 0) {
        return FileUrlFromPath(Join(DistDir(), L"index.html")) +
               L"#/productivity?tab=plan";
      }
      if (_wcsicmp(mode, L"static") == 0) {
        return L"http://127.0.0.1:5174/index.html#/productivity?tab=plan";
      }
    }
    return L"https://calt.app/index.html#/productivity?tab=plan";
  }
  return L"http://127.0.0.1:5173/productivity?tab=plan";
}

std::wstring BibleUrl() {
  if (const wchar_t* env = _wgetenv(L"CALT_BIBLE_URL")) {
    if (env[0]) {
      return env;
    }
  }
  if (HasPrebuiltWebUi()) {
    if (const wchar_t* mode = _wgetenv(L"CALT_FOCUS_UI_MODE")) {
      if (_wcsicmp(mode, L"file") == 0) {
        return FileUrlFromPath(Join(DistDir(), L"index.html")) + L"#/bible";
      }
      if (_wcsicmp(mode, L"static") == 0) {
        return L"http://127.0.0.1:5174/index.html#/bible";
      }
    }
    return L"https://calt.app/index.html#/bible";
  }
  return L"http://127.0.0.1:5173/bible";
}

std::wstring WebViewUserDataDir() {
  const std::wstring dir = Join(DataBehaviorDir(), L"calt_focus_webview");
  CreateDirectoryW(DataBehaviorDir().c_str(), nullptr);
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}
