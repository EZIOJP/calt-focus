#include "pc_apps.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string EscapeJson(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"')
      o += "\\\"";
    else if (c == '\\')
      o += "\\\\";
    else if (c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      o += buf;
    } else
      o.push_back((char)c);
  }
  return o;
}

std::string ToLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return s;
}

bool EndsWithExe(const std::string& n) {
  if (n.size() < 4) return false;
  return ToLower(n.substr(n.size() - 4)) == ".exe";
}

std::string NormalizeExe(std::string name) {
  name = ToLower(name);
  // strip path
  size_t slash = name.find_last_of("\\/");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  if (!EndsWithExe(name)) name += ".exe";
  return name;
}

bool IsProtected(const std::string& exeLower) {
  static const char* kProt[] = {
      "explorer.exe",     "csrss.exe",         "winlogon.exe",     "services.exe",
      "lsass.exe",        "svchost.exe",       "system",           "smss.exe",
      "dwm.exe",          "fontdrvhost.exe",   "calt_focus.exe",   "calt_enforcer.exe",
      "calt_msg_host.exe","calt_sync_host.exe","msedgewebview2.exe","cursor.exe",
      "code.exe",         "devenv.exe",
  };
  for (const char* p : kProt) {
    if (exeLower == p) return true;
  }
  return false;
}

int KillByExe(const std::string& exeLower) {
  if (IsProtected(exeLower)) return 0;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  int n = 0;
  if (Process32FirstW(snap, &pe)) {
    do {
      char narrow[MAX_PATH]{};
      WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, narrow, MAX_PATH, nullptr, nullptr);
      if (ToLower(narrow) != exeLower) continue;
      HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
      if (!h) continue;
      if (TerminateProcess(h, 1)) ++n;
      CloseHandle(h);
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return n;
}

}  // namespace

PcAppsResult PcAppsCloseBrowsers() {
  static const char* kBrowsers[] = {"msedge.exe", "chrome.exe", "firefox.exe", "brave.exe",
                                    "opera.exe",  "opera_gx.exe", "vivaldi.exe", "iexplore.exe"};
  PcAppsResult r;
  int total = 0;
  std::string hit;
  for (const char* b : kBrowsers) {
    int n = KillByExe(b);
    if (n > 0) {
      total += n;
      if (!hit.empty()) hit += ", ";
      hit += b;
      hit += "×";
      hit += std::to_string(n);
    }
  }
  r.closed = total;
  r.ok = true;
  r.detail = total ? ("closed " + hit) : "no browser processes found";
  return r;
}

PcAppsResult PcAppsCloseNamed(const std::vector<std::string>& exes) {
  PcAppsResult r;
  int total = 0;
  std::string hit;
  std::string skipped;
  for (const auto& raw : exes) {
    std::string e = NormalizeExe(raw);
    if (e.empty() || e == ".exe") continue;
    if (IsProtected(e)) {
      if (!skipped.empty()) skipped += ", ";
      skipped += e;
      continue;
    }
    int n = KillByExe(e);
    if (n > 0) {
      total += n;
      if (!hit.empty()) hit += ", ";
      hit += e;
      hit += "×";
      hit += std::to_string(n);
    }
  }
  r.closed = total;
  r.ok = true;
  if (total == 0 && skipped.empty())
    r.detail = "no matching processes";
  else {
    r.detail = total ? ("closed " + hit) : "nothing closed";
    if (!skipped.empty()) r.detail += " (skipped protected: " + skipped + ")";
  }
  return r;
}

std::string PcAppsResultJson(const PcAppsResult& r) {
  return std::string("{\"ok\":") + (r.ok ? "true" : "false") + ",\"closed\":" +
         std::to_string(r.closed) + ",\"detail\":\"" + EscapeJson(r.detail) + "\"}";
}
