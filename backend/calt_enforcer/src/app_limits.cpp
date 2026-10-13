#include "app_limits.h"

#include "kill.h"
#include "softland_publish.h"
#include "sqlite3.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Rule {
  std::string exe;
  int daily_minutes = 0;
};

struct Span {
  long long a = 0;
  long long b = 0;
};

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string LocalDateStr() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
  return buf;
}

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

long long FileTimeToUnixMs(const FILETIME& ft) {
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return (long long)(uli.QuadPart / 10000ULL) - 11644473600000LL;
}

long long UnixNowMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return FileTimeToUnixMs(ft);
}

bool LocalDayUnixRange(long long* startMs, long long* endMs) {
  SYSTEMTIME local{};
  GetLocalTime(&local);
  local.wHour = 0;
  local.wMinute = 0;
  local.wSecond = 0;
  local.wMilliseconds = 0;

  SYSTEMTIME utcStart{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utcStart)) return false;
  FILETIME ft{};
  if (!SystemTimeToFileTime(&utcStart, &ft)) return false;
  *startMs = FileTimeToUnixMs(ft);

  SYSTEMTIME localNext = local;
  FILETIME ftStep{};
  if (!SystemTimeToFileTime(&localNext, &ftStep)) return false;
  ULARGE_INTEGER uli{};
  uli.LowPart = ftStep.dwLowDateTime;
  uli.HighPart = ftStep.dwHighDateTime;
  uli.QuadPart += 864000000000ULL;
  ftStep.dwLowDateTime = uli.LowPart;
  ftStep.dwHighDateTime = uli.HighPart;
  FileTimeToSystemTime(&ftStep, &localNext);
  SYSTEMTIME utcEnd{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &localNext, &utcEnd)) return false;
  if (!SystemTimeToFileTime(&utcEnd, &ft)) return false;
  *endMs = FileTimeToUnixMs(ft);
  return *endMs > *startMs;
}

bool ParseIsoUtcMs(const std::string& iso, long long* out) {
  if (iso.size() < 19 || !out) return false;
  int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0, ms = 0;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) != 6) return false;
  size_t dot = iso.find('.');
  if (dot != std::string::npos && dot + 1 < iso.size()) ms = atoi(iso.c_str() + dot + 1);
  if (Y < 1970 || M < 1 || M > 12 || D < 1 || D > 31) return false;
  SYSTEMTIME st{};
  st.wYear = (WORD)Y;
  st.wMonth = (WORD)M;
  st.wDay = (WORD)D;
  st.wHour = (WORD)h;
  st.wMinute = (WORD)m;
  st.wSecond = (WORD)s;
  st.wMilliseconds = (WORD)(ms > 999 ? 999 : ms);
  FILETIME ft{};
  if (!SystemTimeToFileTime(&st, &ft)) return false;
  *out = FileTimeToUnixMs(ft);
  return true;
}

std::string JsonStringAt(const std::string& body, size_t keyPos) {
  size_t colon = body.find(':', keyPos);
  if (colon == std::string::npos) return {};
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r'))
    ++i;
  if (i >= body.size() || body[i] != '"') return {};
  ++i;
  std::string val;
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
      char e = body[++i];
      if (e == 'n')
        val.push_back('\n');
      else
        val.push_back(e);
      ++i;
      continue;
    }
    val.push_back(body[i++]);
  }
  return val;
}

bool JsonBoolAt(const std::string& body, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
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

long long JsonInt64Near(const std::string& body, const char* key, long long def) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return def;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return def;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size()) return def;
  return std::strtoll(body.c_str() + i, nullptr, 10);
}

std::string BaseExe(std::string s) {
  for (char& c : s) c = (char)tolower((unsigned char)c);
  size_t slash = s.find_last_of("\\/");
  if (slash != std::string::npos) s = s.substr(slash + 1);
  while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '"')) s.pop_back();
  return s;
}

bool ValidExeToken(const std::string& exe) {
  if (exe.size() < 5 || exe.size() > 64) return false;
  if (exe.compare(exe.size() - 4, 4, ".exe") != 0) return false;
  if (!(exe[0] >= 'a' && exe[0] <= 'z') && !(exe[0] >= '0' && exe[0] <= '9')) return false;
  for (char c : exe) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool IsBrowserExe(const std::string& exe) {
  static const char* k[] = {"chrome.exe", "msedge.exe",     "firefox.exe", "brave.exe",
                            "opera.exe",  "vivaldi.exe",    "iexplore.exe", "arc.exe",
                            nullptr};
  for (int i = 0; k[i]; ++i)
    if (exe == k[i]) return true;
  return false;
}

bool IsProtectedExe(const std::string& exe) {
  static const char* k[] = {"explorer.exe", "csrss.exe",        "winlogon.exe", "services.exe",
                            "lsass.exe",    "svchost.exe",      "smss.exe",     "fontdrvhost.exe",
                            "dwm.exe",      "calt_enforcer.exe", "calt_focus.exe", "calt_msg_host.exe",
                            "python.exe",   "pythonw.exe",      nullptr};
  for (int i = 0; k[i]; ++i)
    if (exe == k[i]) return true;
  return false;
}

std::string NormalizeExe(const std::string& raw, std::string* err) {
  std::string s = BaseExe(raw);
  if (s.find('\\') != std::string::npos || s.find('/') != std::string::npos ||
      s.find(':') != std::string::npos) {
    if (err) *err = "bad_exe";
    return {};
  }
  if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".exe") != 0) s += ".exe";
  if (!ValidExeToken(s)) {
    if (err) *err = "bad_exe";
    return {};
  }
  if (IsProtectedExe(s)) {
    if (err) *err = "protected_exe";
    return {};
  }
  if (IsBrowserExe(s)) {
    if (err) *err = "browser_exe";
    return {};
  }
  return s;
}

bool ParseRules(const std::string& body, bool* enabled, std::vector<Rule>* rules, std::string* err) {
  if (enabled) {
    bool en = false;
    if (JsonBoolAt(body, "enabled", &en)) *enabled = en;
  }
  rules->clear();
  std::string needle = "\"limits\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return true;
  size_t lb = body.find('[', p);
  if (lb == std::string::npos) {
    if (err) *err = "bad_payload";
    return false;
  }
  size_t i = lb + 1;
  while (i < body.size()) {
    while (i < body.size() && body[i] != '{' && body[i] != ']') ++i;
    if (i >= body.size() || body[i] == ']') break;
    size_t start = i;
    int depth = 0;
    for (; i < body.size(); ++i) {
      if (body[i] == '{')
        ++depth;
      else if (body[i] == '}') {
        --depth;
        if (depth == 0) {
          ++i;
          break;
        }
      }
    }
    std::string obj = body.substr(start, i - start);
    size_t exeKey = obj.find("\"exe\"");
    if (exeKey == std::string::npos) continue;
    std::string oneErr;
    std::string exe = NormalizeExe(JsonStringAt(obj, exeKey), &oneErr);
    if (exe.empty()) {
      if (err) *err = oneErr.empty() ? "bad_exe" : oneErr;
      return false;
    }
    int minutes = (int)JsonInt64Near(obj, "daily_minutes", 0);
    if (minutes < 1 || minutes > 1440) {
      if (err) *err = "bad_minutes";
      return false;
    }
    bool replaced = false;
    for (auto& r : *rules) {
      if (r.exe == exe) {
        r.daily_minutes = minutes;
        replaced = true;
        break;
      }
    }
    if (!replaced) rules->push_back(Rule{exe, minutes});
    if (rules->size() > 32) {
      if (err) *err = "too_many";
      return false;
    }
  }
  return true;
}

void MergeSpans(std::vector<Span>* spans) {
  if (spans->empty()) return;
  std::sort(spans->begin(), spans->end(), [](const Span& a, const Span& b) { return a.a < b.a; });
  std::vector<Span> out;
  Span cur = (*spans)[0];
  for (size_t i = 1; i < spans->size(); ++i) {
    if ((*spans)[i].a <= cur.b) {
      if ((*spans)[i].b > cur.b) cur.b = (*spans)[i].b;
    } else {
      out.push_back(cur);
      cur = (*spans)[i];
    }
  }
  out.push_back(cur);
  *spans = std::move(out);
}

long long CoveredMs(const std::vector<Span>& spans, long long s, long long e) {
  long long n = 0;
  for (const auto& sp : spans) {
    long long a = sp.a > s ? sp.a : s;
    long long b = sp.b < e ? sp.b : e;
    if (b > a) n += b - a;
  }
  return n;
}

struct LiveFg {
  std::string exe;
  long long startedMs = 0;
  bool active = false;
};

LiveFg ReadLiveFg(const std::wstring& behaviorDir) {
  LiveFg live;
  std::string body = ReadFileUtf8(behaviorDir + L"\\fg_live.json");
  if (body.empty()) return live;
  bool idle = false;
  JsonBoolAt(body, "idle", &idle);
  long long updated = JsonInt64Near(body, "updated_unix_ms", 0);
  long long now = UnixNowMs();
  if (updated <= 0 || now - updated > 10000 || now + 2000 < updated) return live;
  if (idle) return live;
  size_t exeKey = body.find("\"exe\"");
  if (exeKey == std::string::npos) return live;
  live.exe = BaseExe(JsonStringAt(body, exeKey));
  live.startedMs = JsonInt64Near(body, "started_unix_ms", 0);
  if (live.exe.empty() || live.startedMs <= 0) return live;
  if (now - live.startedMs > 12LL * 60 * 60 * 1000) return live;
  if (live.startedMs > now + 2000) return live;
  live.active = true;
  return live;
}

std::map<std::string, long long> UsageMsForRules(const std::wstring& dbPath, const std::vector<Rule>& rules,
                                                const LiveFg& live, long long dayStart, long long dayEnd,
                                                long long now) {
  std::map<std::string, long long> sum;
  std::map<std::string, std::vector<Span>> spans;
  for (const auto& r : rules) sum[r.exe] = 0;
  if (rules.empty() || dbPath.empty() || now <= dayStart) return sum;

  sqlite3* db = nullptr;
  if (sqlite3_open16(dbPath.c_str(), &db) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    db = nullptr;
  }
  if (db) {
    sqlite3_busy_timeout(db, 250);
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT app_name, start_time, end_time FROM tracked_sessions "
        "WHERE start_time < ? AND end_time IS NOT NULL AND end_time != '' AND end_time > ? "
        "AND IFNULL(source,'') IN ('desktop_tracker','selftracker','extension','native');";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
      char startIso[40];
      char endIso[40];
      auto fmt = [](long long ms, char* buf, size_t n) {
        ULARGE_INTEGER uli;
        uli.QuadPart = (unsigned long long)(ms + 11644473600000LL) * 10000ULL;
        FILETIME ft;
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;
        SYSTEMTIME stime{};
        FileTimeToSystemTime(&ft, &stime);
        snprintf(buf, n, "%04u-%02u-%02uT%02u:%02u:%02u.000Z", stime.wYear, stime.wMonth, stime.wDay,
                 stime.wHour, stime.wMinute, stime.wSecond);
      };
      fmt(dayEnd, endIso, sizeof(endIso));
      fmt(dayStart, startIso, sizeof(startIso));
      sqlite3_bind_text(st, 1, endIso, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(st, 2, startIso, -1, SQLITE_TRANSIENT);
      while (sqlite3_step(st) == SQLITE_ROW) {
        const char* app = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        const char* a = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* b = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        if (!app || !a || !b) continue;
        std::string exe = BaseExe(app);
        if (sum.find(exe) == sum.end()) continue;
        long long sa = 0, sb = 0;
        if (!ParseIsoUtcMs(a, &sa) || !ParseIsoUtcMs(b, &sb) || sb <= sa) continue;
        long long s = sa > dayStart ? sa : dayStart;
        long long e = sb < now ? sb : now;
        if (e > dayEnd) e = dayEnd;
        if (e > s) {
          sum[exe] += e - s;
          spans[exe].push_back(Span{s, e});
        }
      }
      sqlite3_finalize(st);
    }
    sqlite3_close(db);
  }

  if (live.active && sum.find(live.exe) != sum.end()) {
    long long s = live.startedMs > dayStart ? live.startedMs : dayStart;
    long long e = now < dayEnd ? now : dayEnd;
    if (e > s) {
      MergeSpans(&spans[live.exe]);
      long long add = (e - s) - CoveredMs(spans[live.exe], s, e);
      if (add > 0) sum[live.exe] += add;
    }
  }
  return sum;
}

std::string RenderDoc(bool enabled, const std::vector<Rule>& rules,
                      const std::map<std::string, long long>& usedMs) {
  std::ostringstream o;
  o << "{\"schema_version\":1,\"enabled\":" << (enabled ? "true" : "false") << ",\"date\":\""
    << LocalDateStr() << "\",\"updated_at\":\"" << IsoLocalNow() << "\",\"limits\":[";
  for (size_t i = 0; i < rules.size(); ++i) {
    if (i) o << ",";
    o << "{\"exe\":\"" << rules[i].exe << "\",\"daily_minutes\":" << rules[i].daily_minutes << "}";
  }
  o << "],\"usage\":[";
  for (size_t i = 0; i < rules.size(); ++i) {
    if (i) o << ",";
    long long ms = 0;
    auto it = usedMs.find(rules[i].exe);
    if (it != usedMs.end()) ms = it->second;
    long long limitSec = (long long)rules[i].daily_minutes * 60LL;
    long long usedSec = ms / 1000LL;
    if (usedSec < 0) usedSec = 0;
    bool over = usedSec >= limitSec;
    long long remain = over ? 0 : (limitSec - usedSec);
    o << "{\"exe\":\"" << rules[i].exe << "\",\"daily_minutes\":" << rules[i].daily_minutes
      << ",\"used_seconds\":" << usedSec << ",\"limit_seconds\":" << limitSec
      << ",\"remaining_seconds\":" << remain << ",\"over_limit\":" << (over ? "true" : "false")
      << ",\"blocked\":" << ((enabled && over) ? "true" : "false") << "}";
  }
  o << "]}";
  return o.str();
}

std::wstring ResolveDb(const std::wstring& dbPath, const std::wstring& behaviorDir) {
  if (!dbPath.empty() && GetFileAttributesW(dbPath.c_str()) != INVALID_FILE_ATTRIBUTES) return dbPath;
  std::wstring dataDir = behaviorDir;
  size_t slash = dataDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
  std::wstring prod = dataDir + L"\\productivity.db";
  if (GetFileAttributesW(prod.c_str()) != INVALID_FILE_ATTRIBUTES) return prod;
  std::wstring legacy = dataDir + L"\\vocab_app.db";
  if (GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) return legacy;
  return prod;
}

struct Evaluated {
  bool enabled = false;
  std::vector<Rule> rules;
  std::map<std::string, long long> usedMs;
  std::string json;
  bool ok = false;
};

Evaluated Evaluate(const std::wstring& dbPath, const std::wstring& behaviorDir, bool enabled,
                   const std::vector<Rule>& rules) {
  Evaluated ev;
  ev.enabled = enabled;
  ev.rules = rules;
  long long dayStart = 0, dayEnd = 0;
  long long now = UnixNowMs();
  if (!LocalDayUnixRange(&dayStart, &dayEnd)) {
    dayStart = now;
    dayEnd = now;
  }
  if (now > dayEnd) now = dayEnd;
  LiveFg live = ReadLiveFg(behaviorDir);
  ev.usedMs = UsageMsForRules(ResolveDb(dbPath, behaviorDir), rules, live, dayStart, dayEnd, now);
  ev.json = RenderDoc(enabled, rules, ev.usedMs);
  ev.ok = true;
  return ev;
}

std::string gLastWritten;
DWORD gLastWriteMs = 0;

bool Publish(const std::wstring& behaviorDir, const std::string& json, bool force) {
  if (json.empty()) return false;
  if (!force && json == gLastWritten) return true;
  DWORD now = GetTickCount();
  bool hadBlock = gLastWritten.find("\"blocked\":true") != std::string::npos;
  bool hasBlock = json.find("\"blocked\":true") != std::string::npos;
  bool blockedFlip = hadBlock != hasBlock;
  if (!force && !blockedFlip && gLastWriteMs && (now - gLastWriteMs) < 5000) return true;
  if (!WriteBehaviorMirrorFile(behaviorDir, L"app_limits.json", json)) return false;
  gLastWritten = json;
  gLastWriteMs = now;
  return true;
}

}  // namespace

AppLimitTickResult TickAppTimeLimits(const std::wstring& dbPath, const std::wstring& behaviorDir) {
  AppLimitTickResult result;
  if (behaviorDir.empty()) return result;
  std::string body = ReadFileUtf8(behaviorDir + L"\\app_limits.json");
  if (body.empty()) return result;
  bool enabled = false;
  std::vector<Rule> rules;
  std::string err;
  if (!ParseRules(body, &enabled, &rules, &err)) return result;
  Evaluated ev = Evaluate(dbPath, behaviorDir, enabled, rules);
  Publish(behaviorDir, ev.json, false);

  if (!enabled || rules.empty()) return result;
  std::vector<std::wstring> targets;
  for (const auto& r : rules) {
    long long ms = 0;
    auto it = ev.usedMs.find(r.exe);
    if (it != ev.usedMs.end()) ms = it->second;
    if (ms / 1000LL >= (long long)r.daily_minutes * 60LL) {
      targets.emplace_back(r.exe.begin(), r.exe.end());
    }
  }
  if (targets.empty()) return result;
  KillResult kr = KillMatchingExes(targets, L"today's time is up.");
  result.killed = kr.killed;
  result.last_exe = kr.last_exe;
  result.last_pid = kr.last_pid;
  return result;
}

std::string AppLimitsHandleSet(const std::wstring& behaviorDir, const std::wstring& dbPath,
                               const std::string& payload, std::string* mirrorJson) {
  if (behaviorDir.empty()) return "limits_write_failed";
  bool enabled = false;
  if (!JsonBoolAt(payload, "enabled", &enabled)) return "bad_payload";
  std::vector<Rule> rules;
  std::string err;
  if (!ParseRules(payload, &enabled, &rules, &err)) return err.empty() ? "bad_payload" : err;
  Evaluated ev = Evaluate(dbPath, behaviorDir, enabled, rules);
  if (!Publish(behaviorDir, ev.json, true)) return "limits_write_failed";
  if (mirrorJson) *mirrorJson = ev.json;
  return "";
}
