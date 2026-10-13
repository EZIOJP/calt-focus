#include "work_session.h"

#include "kill.h"
#include "softland_publish.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SessionDoc {
  bool active = false;
  std::string started_at;
  std::string ends_at;
  std::vector<std::string> apps;
  std::vector<std::string> sites;
};

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

std::string AddMinutesLocal(int minutes) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  FILETIME ft{};
  if (!SystemTimeToFileTime(&st, &ft)) return IsoLocalNow();
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  uli.QuadPart += (unsigned long long)minutes * 60ULL * 10000000ULL;
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  FileTimeToSystemTime(&ft, &st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

long long UnixNowMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return (long long)(uli.QuadPart / 10000ULL) - 11644473600000LL;
}

bool JsonBoolAt(const std::string& body, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r'))
    ++i;
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

std::string JsonStringAt(const std::string& body, const char* key) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return {};
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return {};
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size() || body[i] != '"') return {};
  ++i;
  std::string val;
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
      val.push_back(body[++i]);
      ++i;
      continue;
    }
    val.push_back(body[i++]);
  }
  return val;
}

std::string BaseLower(std::string s) {
  for (char& c : s) c = (char)tolower((unsigned char)c);
  size_t slash = s.find_last_of("\\/");
  if (slash != std::string::npos) s = s.substr(slash + 1);
  while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '"')) s.pop_back();
  return s;
}

bool ValidExe(const std::string& exe) {
  if (exe.size() < 5 || exe.size() > 64) return false;
  if (exe.compare(exe.size() - 4, 4, ".exe") != 0) return false;
  if (!((exe[0] >= 'a' && exe[0] <= 'z') || (exe[0] >= '0' && exe[0] <= '9'))) return false;
  for (char c : exe) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool IsProtectedExe(const std::string& exe) {
  static const char* k[] = {"explorer.exe", "csrss.exe",         "winlogon.exe",
                            "services.exe", "lsass.exe",         "svchost.exe",
                            "smss.exe",     "fontdrvhost.exe",   "dwm.exe",
                            "calt_enforcer.exe", "calt_focus.exe", "calt_msg_host.exe",
                            "python.exe",   "pythonw.exe",       nullptr};
  for (int i = 0; k[i]; ++i)
    if (exe == k[i]) return true;
  return false;
}

bool AlwaysAllowedForeground(const std::string& exe) {
  static const char* k[] = {
      "explorer.exe",
      "searchhost.exe",
      "searchapp.exe",
      "startmenuexperiencehost.exe",
      "shellexperiencehost.exe",
      "applicationframehost.exe",
      "textinputhost.exe",
      "lockapp.exe",
      "systemsettings.exe",
      "dwm.exe",
      "sihost.exe",
      "ctfmon.exe",
      "pickerhost.exe",
      "runtimebroker.exe",
      "calt_focus.exe",
      "calt_enforcer.exe",
      "calt_msg_host.exe",
      nullptr};
  for (int i = 0; k[i]; ++i)
    if (exe == k[i]) return true;
  return false;
}

bool IsBrowserExe(const std::string& exe) {
  static const char* k[] = {"chrome.exe", "msedge.exe",  "firefox.exe", "brave.exe",
                            "opera.exe",  "vivaldi.exe", "iexplore.exe", "arc.exe",
                            nullptr};
  for (int i = 0; k[i]; ++i)
    if (exe == k[i]) return true;
  return false;
}

std::string NormalizeExe(const std::string& raw, std::string* err) {
  std::string s = BaseLower(raw);
  if (s.find('\\') != std::string::npos || s.find('/') != std::string::npos ||
      s.find(':') != std::string::npos) {
    if (err) *err = "bad_exe";
    return {};
  }
  if (s.size() < 4 || s.compare(s.size() - 4, 4, ".exe") != 0) s += ".exe";
  if (!ValidExe(s)) {
    if (err) *err = "bad_exe";
    return {};
  }
  if (IsProtectedExe(s)) {
    if (err) *err = "protected_exe";
    return {};
  }
  return s;
}

std::string NormalizeSite(const std::string& raw, std::string* err) {
  std::string s = raw;
  for (char& c : s) c = (char)tolower((unsigned char)c);
  while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '"' || s.back() == '/')) s.pop_back();
  auto cut = s.find("://");
  if (cut != std::string::npos) s = s.substr(cut + 3);
  auto slash = s.find('/');
  if (slash != std::string::npos) s = s.substr(0, slash);
  auto colon = s.find(':');
  if (colon != std::string::npos) s = s.substr(0, colon);
  if (s.rfind("www.", 0) == 0) s = s.substr(4);
  if (s.size() < 3 || s.size() > 80 || s.find('.') == std::string::npos) {
    if (err) *err = "bad_site";
    return {};
  }
  for (char c : s) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok) {
      if (err) *err = "bad_site";
      return {};
    }
  }
  return s;
}

bool ParseStringArray(const std::string& body, const char* key, std::vector<std::string>* out,
                      const std::string& kind, std::string* err) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return true;
  size_t lb = body.find('[', p);
  if (lb == std::string::npos) {
    if (err) *err = "bad_payload";
    return false;
  }
  size_t i = lb + 1;
  while (i < body.size() && body[i] != ']') {
    if (body[i] == '"') {
      ++i;
      std::string tok;
      while (i < body.size() && body[i] != '"') {
        if (body[i] == '\\' && i + 1 < body.size()) {
          tok.push_back(body[++i]);
          ++i;
          continue;
        }
        tok.push_back(body[i++]);
      }
      if (i < body.size() && body[i] == '"') ++i;
      std::string one;
      std::string norm = (kind == "exe") ? NormalizeExe(tok, &one) : NormalizeSite(tok, &one);
      if (norm.empty()) {
        if (err) *err = one.empty() ? "bad_payload" : one;
        return false;
      }
      if (std::find(out->begin(), out->end(), norm) == out->end()) out->push_back(norm);
      if (out->size() > 24) {
        if (err) *err = "too_many";
        return false;
      }
      continue;
    }
    ++i;
  }
  return true;
}

bool LoadDoc(const std::wstring& behaviorDir, SessionDoc* doc) {
  std::string body = ReadFileUtf8(behaviorDir + L"\\work_session.json");
  if (body.empty()) return false;
  JsonBoolAt(body, "active", &doc->active);
  doc->started_at = JsonStringAt(body, "started_at");
  doc->ends_at = JsonStringAt(body, "ends_at");
  std::string ignore;
  ParseStringArray(body, "apps", &doc->apps, "exe", &ignore);
  ParseStringArray(body, "sites", &doc->sites, "site", &ignore);
  return true;
}

std::string Render(const SessionDoc& doc) {
  std::ostringstream o;
  o << "{\"active\":" << (doc.active ? "true" : "false") << ",\"started_at\":\""
    << doc.started_at << "\",\"ends_at\":\"" << doc.ends_at << "\",\"apps\":[";
  for (size_t i = 0; i < doc.apps.size(); ++i) {
    if (i) o << ",";
    o << "\"" << doc.apps[i] << "\"";
  }
  o << "],\"sites\":[";
  for (size_t i = 0; i < doc.sites.size(); ++i) {
    if (i) o << ",";
    o << "\"" << doc.sites[i] << "\"";
  }
  o << "]}";
  return o.str();
}

bool Expired(const std::string& ends) {
  if (ends.empty() || ends == "null") return false;
  return ends <= IsoLocalNow();
}

// A session with no end time must not survive local midnight into the next
// planning phase. Dated ends_at already expire via Expired().
bool StartedBeforeToday(const std::string& started) {
  if (started.size() < 10) return false;
  const std::string now = IsoLocalNow();
  if (now.size() < 10) return false;
  return started.compare(0, 10, now, 0, 10) < 0;
}

struct LiveFg {
  std::string exe;
  bool active = false;
};

LiveFg ReadLive(const std::wstring& behaviorDir) {
  LiveFg live;
  std::string body = ReadFileUtf8(behaviorDir + L"\\fg_live.json");
  if (body.empty()) return live;
  bool idle = false;
  JsonBoolAt(body, "idle", &idle);
  if (idle) return live;
  long long updated = 0;
  std::string needle = "\"updated_unix_ms\"";
  size_t p = body.find(needle);
  if (p != std::string::npos) {
    size_t colon = body.find(':', p + needle.size());
    if (colon != std::string::npos) updated = std::strtoll(body.c_str() + colon + 1, nullptr, 10);
  }
  long long now = UnixNowMs();
  if (updated <= 0 || now - updated > 10000) return live;
  live.exe = BaseLower(JsonStringAt(body, "exe"));
  if (live.exe.empty()) return live;
  live.active = true;
  return live;
}

bool AllowedApp(const SessionDoc& doc, const std::string& exe) {
  if (AlwaysAllowedForeground(exe)) return true;
  if (std::find(doc.apps.begin(), doc.apps.end(), exe) != doc.apps.end()) return true;
  if (!doc.sites.empty() && IsBrowserExe(exe)) return true;
  return false;
}

}  // namespace

WorkSessionTickResult TickWorkSession(const std::wstring& behaviorDir) {
  WorkSessionTickResult result;
  if (behaviorDir.empty()) return result;
  SessionDoc doc;
  if (!LoadDoc(behaviorDir, &doc) || !doc.active) return result;
  if (Expired(doc.ends_at) || (doc.ends_at.empty() && StartedBeforeToday(doc.started_at))) {
    doc.active = false;
    doc.ends_at.clear();
    WriteBehaviorMirrorFile(behaviorDir, L"work_session.json", Render(doc));
    return result;
  }
  LiveFg live = ReadLive(behaviorDir);
  if (!live.active || AllowedApp(doc, live.exe)) return result;
  std::vector<std::wstring> targets;
  targets.emplace_back(live.exe.begin(), live.exe.end());
  KillResult kr = KillMatchingExes(targets, L"outside this work session.");
  result.killed = kr.killed;
  result.last_exe = kr.last_exe;
  result.last_pid = kr.last_pid;
  return result;
}

std::string WorkSessionHandleSet(const std::wstring& behaviorDir, const std::string& payload,
                                 std::string* mirrorJson) {
  if (behaviorDir.empty()) return "session_write_failed";
  bool active = false;
  if (!JsonBoolAt(payload, "active", &active)) return "bad_payload";
  SessionDoc doc;
  std::string err;
  if (!ParseStringArray(payload, "apps", &doc.apps, "exe", &err)) return err.empty() ? "bad_payload" : err;
  if (!ParseStringArray(payload, "sites", &doc.sites, "site", &err))
    return err.empty() ? "bad_payload" : err;
  if (active && doc.apps.empty() && doc.sites.empty()) return "empty_session";
  doc.active = active;
  if (active) {
    doc.started_at = IsoLocalNow();
    int minutes = 0;
    std::string needle = "\"minutes\"";
    size_t p = payload.find(needle);
    if (p != std::string::npos) {
      size_t colon = payload.find(':', p + needle.size());
      if (colon != std::string::npos) minutes = atoi(payload.c_str() + colon + 1);
    }
    if (minutes < 0 || minutes > 720) return "bad_minutes";
    doc.ends_at = minutes > 0 ? AddMinutesLocal(minutes) : "";
  } else {
    SessionDoc prev;
    // Ending a live session keeps the lists that were in force. Idle draft
    // saves (active:false while not live) keep the payload apps/sites so Home
    // Allow/Block can update the allowlist without starting a session.
    if (LoadDoc(behaviorDir, &prev) && prev.active) {
      doc.apps = prev.apps;
      doc.sites = prev.sites;
      doc.started_at = prev.started_at;
    } else {
      doc.started_at.clear();
    }
    doc.ends_at.clear();
  }
  std::string json = Render(doc);
  if (!WriteBehaviorMirrorFile(behaviorDir, L"work_session.json", json)) return "session_write_failed";
  if (mirrorJson) *mirrorJson = json;
  return "";
}
