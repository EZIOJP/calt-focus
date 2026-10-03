#include "status_badge.h"

#include "paths.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace {

bool JsonBool(const std::string& body, const char* key, bool def = false) {
  const std::string needle = std::string("\"") + key + "\"";
  const auto pos = body.find(needle);
  if (pos == std::string::npos) {
    return def;
  }
  const auto colon = body.find(':', pos + needle.size());
  if (colon == std::string::npos) {
    return def;
  }
  const auto t = body.find("true", colon);
  const auto f = body.find("false", colon);
  if (t != std::string::npos && (f == std::string::npos || t < f)) {
    if (t - colon < 32) {
      return true;
    }
  }
  if (f != std::string::npos && f - colon < 32) {
    return false;
  }
  return def;
}

std::wstring JsonString(const std::string& body, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  const auto pos = body.find(needle);
  if (pos == std::string::npos) {
    return L"";
  }
  const auto colon = body.find(':', pos + needle.size());
  if (colon == std::string::npos) {
    return L"";
  }
  const auto q1 = body.find('"', colon + 1);
  if (q1 == std::string::npos) {
    return L"";
  }
  const auto q2 = body.find('"', q1 + 1);
  if (q2 == std::string::npos || q2 <= q1 + 1) {
    return L"";
  }
  const std::string raw = body.substr(q1 + 1, q2 - q1 - 1);
  if (raw.empty()) {
    return L"";
  }
  return std::wstring(raw.begin(), raw.end());
}

// Rough age from ISO "YYYY-MM-DDTHH:MM:SSZ" vs local UTC now (seconds). -1 if unparsable.
long StatusAgeSeconds(const std::wstring& iso) {
  if (iso.size() < 19) return -1;
  SYSTEMTIME st{};
  st.wYear = (WORD)_wtoi(iso.substr(0, 4).c_str());
  st.wMonth = (WORD)_wtoi(iso.substr(5, 2).c_str());
  st.wDay = (WORD)_wtoi(iso.substr(8, 2).c_str());
  st.wHour = (WORD)_wtoi(iso.substr(11, 2).c_str());
  st.wMinute = (WORD)_wtoi(iso.substr(14, 2).c_str());
  st.wSecond = (WORD)_wtoi(iso.substr(17, 2).c_str());
  FILETIME ft{};
  if (!SystemTimeToFileTime(&st, &ft)) return -1;
  ULARGE_INTEGER then{};
  then.LowPart = ft.dwLowDateTime;
  then.HighPart = ft.dwHighDateTime;
  FILETIME nowFt{};
  GetSystemTimeAsFileTime(&nowFt);
  ULARGE_INTEGER now{};
  now.LowPart = nowFt.dwLowDateTime;
  now.HighPart = nowFt.dwHighDateTime;
  if (now.QuadPart < then.QuadPart) return 0;
  return (long)((now.QuadPart - then.QuadPart) / 10000000ULL);
}

}  // namespace

bool EnforcerBadge::Fresh() const {
  if (!ok) return false;
  if (updated_at.empty()) return owns;  // legacy mirrors without stamp
  long age = StatusAgeSeconds(updated_at);
  if (age < 0) return owns;
  return age <= 45;
}

const wchar_t* EnforcerBadge::LiveLabel() const {
  if (!ok) return L"DOWN";
  if (owns && Fresh()) return L"LIVE";
  if (ok && Fresh()) return L"LIVE";
  if (ok) return L"STALE";
  return L"DOWN";
}

EnforcerBadge ReadEnforcerBadge() {
  EnforcerBadge b;
  const std::wstring path = EnforcerStatusPath();
  std::ifstream in(path.c_str());
  if (!in) {
    return b;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string body = ss.str();
  if (body.empty()) {
    return b;
  }
  b.ok = true;
  b.owns = JsonBool(body, "owns");
  b.armed = JsonBool(body, "armed");
  b.softland_or_armed = JsonBool(body, "softland_or_armed");
  b.service_running = JsonBool(body, "service_running");
  b.last_kill_exe = JsonString(body, "last_kill_exe");
  if (b.last_kill_exe.empty()) {
    b.last_kill_exe = JsonString(body, "last_kill");
  }
  b.updated_at = JsonString(body, "updated_at");
  return b;
}

bool FocusBlocksActive() {
  const EnforcerBadge b = ReadEnforcerBadge();
  if (b.ok && b.softland_or_armed) {
    return true;
  }
  if (b.ok && b.armed) {
    return true;
  }
  const std::wstring path = SoftlandPolicyPath();
  std::ifstream in(path.c_str());
  if (!in) {
    return b.armed;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return JsonBool(ss.str(), "softland_enabled") || b.armed;
}

std::wstring FormatTrayTooltip(const EnforcerBadge& b) {
  if (!b.ok) {
    return L"CALT Focus — Enforcer: DOWN · Tracker: DOWN";
  }
  // Desktop tracker is the enforcer session loop — same live signal.
  const wchar_t* live = b.LiveLabel();
  std::wstring tip = L"CALT Focus — Enforcer: ";
  tip += live;
  tip += L" · Tracker: ";
  tip += live;
  tip += b.armed ? L" · Armed" : L" · Disarmed";
  if (!b.last_kill_exe.empty()) {
    tip += L" · last ";
    tip += b.last_kill_exe;
  }
  return tip;
}
