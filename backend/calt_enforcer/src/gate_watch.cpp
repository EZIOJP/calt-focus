#include "gate_watch.h"

#include "kill.h"
#include "productivity_store.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Match Study comms_health: Gate poll ~8–12s; MV3 sleep can stretch; dead at 3 min.
constexpr double kDeadS = 180.0;
constexpr DWORD kStartupGraceMs = 90000;
constexpr int kDeadStrikesNeeded = 2;
constexpr DWORD kCloseCooldownMs = 120000;

DWORD gProcessStartMs = 0;
int gDeadStrikes = 0;
DWORD gLastCloseMs = 0;

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool JsonStringNear(const std::string& body, const char* key, std::string* out) {
  if (!out) return false;
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() &&
         (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n'))
    ++i;
  if (i >= body.size() || body[i] != '"') return false;
  ++i;
  std::string tok;
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
      tok.push_back(body[i + 1]);
      i += 2;
      continue;
    }
    tok.push_back(body[i++]);
  }
  *out = tok;
  return true;
}

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
  return buf;
}

std::string NormalizeIsoForCompare(std::string s) {
  if (s.empty() || s == "null") return {};
  if (!s.empty() && (s.back() == 'Z' || s.back() == 'z')) s.pop_back();
  size_t plus = s.find('+');
  if (plus != std::string::npos && plus > 10) s = s.substr(0, plus);
  if (s.size() > 19) s = s.substr(0, 19);
  return s;
}

bool FreeUntilActive(const std::string& free_until) {
  std::string u = NormalizeIsoForCompare(free_until);
  if (u.empty()) return false;
  return u > IsoLocalNow();
}

/** Age in seconds of ISO-Z heartbeat `at` field; negative if unparseable/missing. */
double HeartbeatAgeS(const std::string& body) {
  std::string at;
  if (!JsonStringNear(body, "at", &at) || at.size() < 19) return -1.0;
  SYSTEMTIME st{};
  st.wYear = (WORD)atoi(at.c_str());
  st.wMonth = (WORD)atoi(at.c_str() + 5);
  st.wDay = (WORD)atoi(at.c_str() + 8);
  st.wHour = (WORD)atoi(at.c_str() + 11);
  st.wMinute = (WORD)atoi(at.c_str() + 14);
  st.wSecond = (WORD)atoi(at.c_str() + 17);
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) return -1.0;
  FILETIME nowFt;
  GetSystemTimeAsFileTime(&nowFt);
  ULARGE_INTEGER a{}, b{};
  a.LowPart = ft.dwLowDateTime;
  a.HighPart = ft.dwHighDateTime;
  b.LowPart = nowFt.dwLowDateTime;
  b.HighPart = nowFt.dwHighDateTime;
  if (b.QuadPart < a.QuadPart) return 0.0;
  return (double)(b.QuadPart - a.QuadPart) / 10000000.0;
}

void AppendGateCloseLog(const std::wstring& behaviorDir, int killed) {
  std::wstring path = behaviorDir + L"\\enforcer_kills.log";
  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, path.c_str(), L"ab");
#else
  f = _wfopen(path.c_str(), L"ab");
#endif
  if (!f) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u gate_watch close msedge killed=%d\n", st.wYear,
          st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, killed);
  fclose(f);
}

}  // namespace

GateWatchTickResult TickGateWatch(const std::wstring& behaviorDir) {
  GateWatchTickResult out;
  if (gProcessStartMs == 0) gProcessStartMs = GetTickCount();

  ProductivitySoftland soft;
  if (!ProductivityLoadSoftland(soft) || !soft.softland_enabled) {
    gDeadStrikes = 0;
    return out;
  }
  // Free / reward browsing: never close Edge for a missing extension.
  if (soft.reward_day_active || FreeUntilActive(soft.free_until)) {
    gDeadStrikes = 0;
    return out;
  }

  const DWORD nowMs = GetTickCount();
  if (nowMs - gProcessStartMs < kStartupGraceMs) {
    gDeadStrikes = 0;
    return out;
  }

  const std::string hb = ReadFileUtf8(behaviorDir + L"\\gate_heartbeat.json");
  const double age = HeartbeatAgeS(hb);
  const bool dead = (age < 0.0) || (age > kDeadS);
  out.candidate = dead;
  if (!dead) {
    gDeadStrikes = 0;
    out.strikes = 0;
    return out;
  }

  ++gDeadStrikes;
  out.strikes = gDeadStrikes;
  if (gDeadStrikes < kDeadStrikesNeeded) return out;
  if (gLastCloseMs && (nowMs - gLastCloseMs) < kCloseCooldownMs) return out;

  KillResult kr = KillMatchingExes({L"msedge.exe"}, L"Gate extension not running.");
  out.killed = kr.killed;
  out.last_exe = kr.last_exe;
  out.last_pid = kr.last_pid;
  if (kr.killed > 0) {
    gLastCloseMs = nowMs;
    gDeadStrikes = 0;
    AppendGateCloseLog(behaviorDir, kr.killed);
  }
  return out;
}
