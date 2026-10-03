#include "status_writer.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(c);
    } else if (c == '\n') {
      o += "\\n";
    } else if ((unsigned char)c < 0x20) {
      /* skip */
    } else {
      o.push_back(c);
    }
  }
  return o;
}

}  // namespace

std::wstring DefaultStatusJsonPath(const std::wstring& dbPath) {
  size_t slash = dbPath.find_last_of(L"\\/");
  std::wstring dataDir = (slash == std::wstring::npos) ? L"." : dbPath.substr(0, slash);
  return dataDir + L"\\behavior\\enforcer_status.json";
}

bool WriteEnforcerStatus(const std::wstring& statusPath, const EnforcerStatusSnapshot& snap) {
  size_t slash = statusPath.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    CreateDirectoryW(statusPath.substr(0, slash).c_str(), nullptr);
  }

  SYSTEMTIME st;
  GetSystemTime(&st);
  char iso[40];
  snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);

  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, statusPath.c_str(), L"wb");
#else
  f = _wfopen(statusPath.c_str(), L"wb");
#endif
  if (!f) return false;

  fprintf(f,
          "{\n"
          "  \"owns\": %s,\n"
          "  \"pid\": %lu,\n"
          "  \"last_kill\": \"%s\",\n"
          "  \"last_kill_exe\": \"%s\",\n"
          "  \"lock_present\": %s,\n"
          "  \"service_running\": %s,\n"
          "  \"armed\": %s,\n"
          "  \"locked\": %s,\n"
          "  \"incubation\": %s,\n"
          "  \"policy_source\": \"%s\",\n"
          "  \"focus_running\": %s,\n"
          "  \"softland_or_armed\": %s,\n"
          "  \"focus_relaunch_count\": %u,\n"
          "  \"focus_last_relaunch_at\": \"%s\",\n"
          "  \"focus_relaunch_suppressed\": %s,\n"
          "  \"updated_at\": \"%s\"\n"
          "}\n",
          snap.owns ? "true" : "false", snap.pid, JsonEscape(snap.last_kill).c_str(),
          JsonEscape(snap.last_kill_exe).c_str(), snap.lock_present ? "true" : "false",
          snap.service_running ? "true" : "false", snap.armed ? "true" : "false",
          snap.locked ? "true" : "false", snap.incubation ? "true" : "false",
          JsonEscape(snap.policy_source).c_str(), snap.focus_running ? "true" : "false",
          snap.softland_or_armed ? "true" : "false", snap.focus_relaunch_count,
          JsonEscape(snap.focus_last_relaunch_at).c_str(),
          snap.focus_relaunch_suppressed ? "true" : "false", iso);

  fclose(f);
  return true;
}
