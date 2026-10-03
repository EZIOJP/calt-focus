#include "day_rollup.h"
#include "sqlite3.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Rule {
  std::regex re;
  std::string category;
  int score = 35;
  bool ok = false;
};

struct Ruleset {
  int threshold = 60;
  int default_score = 35;
  std::vector<Rule> app;
  std::vector<Rule> domain;
  // category -> score from JSON map (simple linear scan)
  std::vector<std::pair<std::string, int>> scores;
  bool loaded = false;
};

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::string LocalDateStr() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
  return buf;
}

// Local calendar day [start,end) as UTC ISO-Z for comparing against tracked_sessions (Z stamps).
void LocalDayUtcBoundsZ(std::string& dayStartZ, std::string& dayEndZ) {
  SYSTEMTIME localMidnight{};
  GetLocalTime(&localMidnight);
  localMidnight.wHour = 0;
  localMidnight.wMinute = 0;
  localMidnight.wSecond = 0;
  localMidnight.wMilliseconds = 0;

  SYSTEMTIME utcStart{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &localMidnight, &utcStart)) {
    // Fallback: treat local date digits as UTC (legacy behavior).
    std::string today = LocalDateStr();
    dayStartZ = today + "T00:00:00.000Z";
    dayEndZ = today + "T23:59:59.999Z";
    return;
  }

  FILETIME ft{};
  SystemTimeToFileTime(&utcStart, &ft);
  ULARGE_INTEGER uli{};
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  // +1 local day ≈ add 24h in UTC filetime (DST edges: re-convert next local midnight).
  SYSTEMTIME localNext = localMidnight;
  FILETIME ftLocal{};
  SystemTimeToFileTime(&localNext, &ftLocal);
  ULARGE_INTEGER uliLocal{};
  uliLocal.LowPart = ftLocal.dwLowDateTime;
  uliLocal.HighPart = ftLocal.dwHighDateTime;
  uliLocal.QuadPart += 864000000000ULL;  // 24h in 100ns
  ftLocal.dwLowDateTime = uliLocal.LowPart;
  ftLocal.dwHighDateTime = uliLocal.HighPart;
  FileTimeToSystemTime(&ftLocal, &localNext);
  SYSTEMTIME utcEnd{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &localNext, &utcEnd)) {
    uli.QuadPart += 864000000000ULL;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    FileTimeToSystemTime(&ft, &utcEnd);
  }

  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", utcStart.wYear, utcStart.wMonth,
           utcStart.wDay, utcStart.wHour, utcStart.wMinute, utcStart.wSecond, utcStart.wMilliseconds);
  dayStartZ = buf;
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", utcEnd.wYear, utcEnd.wMonth,
           utcEnd.wDay, utcEnd.wHour, utcEnd.wMinute, utcEnd.wSecond, utcEnd.wMilliseconds);
  dayEndZ = buf;
}

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool WriteFileUtf8(const std::wstring& path, const std::string& body) {
  std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(body.data(), (std::streamsize)body.size());
  return (bool)out;
}

// Minimal JSON helpers (same style as rest of enforcer).
bool JsonGetIntNear(const std::string& body, const char* key, int* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size()) return false;
  *out = atoi(body.c_str() + i);
  return true;
}

std::string JsonGetStringInObject(const std::string& obj, const char* key) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = obj.find(needle);
  if (p == std::string::npos) return {};
  size_t colon = obj.find(':', p + needle.size());
  if (colon == std::string::npos) return {};
  size_t i = colon + 1;
  while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
  if (i >= obj.size() || obj[i] != '"') return {};
  ++i;
  std::string val;
  while (i < obj.size() && obj[i] != '"') {
    if (obj[i] == '\\' && i + 1 < obj.size()) {
      val.push_back(obj[i + 1]);
      i += 2;
      continue;
    }
    val.push_back(obj[i++]);
  }
  return val;
}

int JsonGetIntInObject(const std::string& obj, const char* key, int def) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = obj.find(needle);
  if (p == std::string::npos) return def;
  size_t colon = obj.find(':', p + needle.size());
  if (colon == std::string::npos) return def;
  size_t i = colon + 1;
  while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
  if (i >= obj.size()) return def;
  return atoi(obj.c_str() + i);
}

void ParseRuleArray(const std::string& body, const char* arrayKey, std::vector<Rule>* out) {
  std::string needle = std::string("\"") + arrayKey + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return;
  size_t lb = body.find('[', p);
  if (lb == std::string::npos) return;
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
    std::string pat = JsonGetStringInObject(obj, "pattern");
    std::string cat = JsonGetStringInObject(obj, "category");
    int score = JsonGetIntInObject(obj, "score", 35);
    if (pat.empty() || cat.empty()) continue;
    Rule r;
    r.category = cat;
    r.score = score;
    try {
      r.re = std::regex(pat, std::regex::icase | std::regex::ECMAScript | std::regex::optimize);
      r.ok = true;
    } catch (...) {
      r.ok = false;
    }
    if (r.ok) out->push_back(std::move(r));
  }
}

void ParseScoreMap(const std::string& body, std::vector<std::pair<std::string, int>>* out) {
  std::string needle = "\"category_scores\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return;
  size_t lb = body.find('{', p);
  if (lb == std::string::npos) return;
  size_t i = lb + 1;
  int depth = 1;
  size_t end = lb + 1;
  for (; end < body.size() && depth > 0; ++end) {
    if (body[end] == '{')
      ++depth;
    else if (body[end] == '}')
      --depth;
  }
  std::string obj = body.substr(lb, end - lb);
  // Scan "key": number pairs
  size_t q = 0;
  while (true) {
    size_t q1 = obj.find('"', q);
    if (q1 == std::string::npos) break;
    size_t q2 = obj.find('"', q1 + 1);
    if (q2 == std::string::npos) break;
    std::string key = obj.substr(q1 + 1, q2 - q1 - 1);
    size_t colon = obj.find(':', q2);
    if (colon == std::string::npos) break;
    int score = atoi(obj.c_str() + colon + 1);
    if (!key.empty()) out->push_back({key, score});
    q = colon + 1;
  }
}

Ruleset gRules;
DWORD gRulesLoadedTick = 0;

void EnsureRules(const std::wstring& behaviorDir) {
  DWORD now = GetTickCount();
  if (gRules.loaded && (now - gRulesLoadedTick) < 30000) return;
  gRules = Ruleset{};
  std::string body = ReadFileUtf8(Join(behaviorDir, L"classify_rules.json"));
  if (body.empty()) {
    gRules.loaded = true;
    gRulesLoadedTick = now;
    return;
  }
  JsonGetIntNear(body, "productive_threshold", &gRules.threshold);
  JsonGetIntNear(body, "default_score", &gRules.default_score);
  if (gRules.threshold <= 0) gRules.threshold = 60;
  ParseRuleArray(body, "app_rules", &gRules.app);
  ParseRuleArray(body, "domain_rules", &gRules.domain);
  ParseScoreMap(body, &gRules.scores);
  gRules.loaded = true;
  gRulesLoadedTick = now;
}

int ScoreForCategory(const std::string& cat) {
  for (const auto& kv : gRules.scores) {
    if (kv.first == cat) return kv.second;
  }
  return gRules.default_score;
}

std::pair<std::string, int> ClassifyHay(const std::string& hay, const std::vector<Rule>& rules) {
  for (const auto& r : rules) {
    if (!r.ok) continue;
    try {
      if (std::regex_search(hay, r.re)) return {r.category, r.score};
    } catch (...) {
    }
  }
  return {"Uncategorized", gRules.default_score};
}

bool IsBrowserExe(const std::string& exeLower) {
  static const char* k[] = {"chrome", "msedge", "firefox", "brave", "opera", "vivaldi",
                            "arc",     "zen",    "chromium", nullptr};
  for (int i = 0; k[i]; ++i) {
    if (exeLower.find(k[i]) != std::string::npos) return true;
  }
  return false;
}

std::pair<std::string, int> ClassifySession(const std::string& app, const std::string& title) {
  std::string exe = app;
  for (auto& c : exe) c = (char)tolower((unsigned char)c);
  std::string hay = exe + " " + title;
  for (auto& c : hay) c = (char)tolower((unsigned char)c);
  if (IsBrowserExe(exe)) {
    auto d = ClassifyHay(hay, gRules.domain);
    if (d.first != "Uncategorized") return d;
    return ClassifyHay(hay, gRules.app);
  }
  return ClassifyHay(hay, gRules.app);
}

struct Interval {
  long long a = 0;
  long long b = 0;
};

long long ParseIsoToUnixApprox(const std::string& iso) {
  // Accept YYYY-MM-DDTHH:MM:SS… (UTC or local — treat as wall for merge only)
  if (iso.size() < 19) return 0;
  int Y = atoi(iso.substr(0, 4).c_str());
  int M = atoi(iso.substr(5, 2).c_str());
  int D = atoi(iso.substr(8, 2).c_str());
  int h = atoi(iso.substr(11, 2).c_str());
  int m = atoi(iso.substr(14, 2).c_str());
  int s = atoi(iso.substr(17, 2).c_str());
  SYSTEMTIME st{};
  st.wYear = (WORD)Y;
  st.wMonth = (WORD)M;
  st.wDay = (WORD)D;
  st.wHour = (WORD)h;
  st.wMinute = (WORD)m;
  st.wSecond = (WORD)s;
  FILETIME ft;
  // Treat as UTC for ordering consistency with Z stamps; local stamps still order correctly same day.
  SystemTimeToFileTime(&st, &ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return (long long)((uli.QuadPart / 10000000ULL) - 11644473600ULL);
}

long long MergeSeconds(std::vector<Interval>& iv) {
  if (iv.empty()) return 0;
  std::sort(iv.begin(), iv.end(), [](const Interval& x, const Interval& y) { return x.a < y.a; });
  long long total = 0;
  long long curA = iv[0].a, curB = iv[0].b;
  for (size_t i = 1; i < iv.size(); ++i) {
    if (iv[i].a <= curB) {
      if (iv[i].b > curB) curB = iv[i].b;
    } else {
      total += (curB - curA);
      curA = iv[i].a;
      curB = iv[i].b;
    }
  }
  total += (curB - curA);
  return total > 0 ? total : 0;
}

}  // namespace

void TickDayRollup(const std::wstring& dbPath, const std::wstring& behaviorDir) {
  EnsureRules(behaviorDir);
  if (dbPath.empty()) return;

  sqlite3* db = nullptr;
  if (sqlite3_open16(dbPath.c_str(), &db) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return;
  }

  const std::string today = LocalDateStr();
  std::string dayStartZ;
  std::string dayEndZ;
  LocalDayUtcBoundsZ(dayStartZ, dayEndZ);

  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT session_id, app_name, window_title, category, start_time, end_time, "
      "override_productive "
      "FROM tracked_sessions "
      "WHERE start_time < ? AND (end_time IS NULL OR end_time = '' OR end_time > ?) "
      "AND IFNULL(source,'') IN ('desktop_tracker','selftracker','extension','native');";
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return;
  }
  sqlite3_bind_text(st, 1, dayEndZ.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, dayStartZ.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<Interval> productive;
  int sessionCount = 0;
  while (sqlite3_step(st) == SQLITE_ROW) {
    ++sessionCount;
    sqlite3_int64 sid = sqlite3_column_int64(st, 0);
    const char* app = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
    const char* start = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
    const char* end = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
    int overrideCol = sqlite3_column_type(st, 6);
    int overrideVal = overrideCol == SQLITE_NULL ? -1 : sqlite3_column_int(st, 6);

    std::string category = cat ? cat : "";
    int score = gRules.default_score;
    if (category.empty()) {
      auto cl = ClassifySession(app ? app : "", title ? title : "");
      category = cl.first;
      score = cl.second;
      sqlite3_stmt* up = nullptr;
      if (sqlite3_prepare_v2(db,
                             "UPDATE tracked_sessions SET category=?, category_source='native-rules' "
                             "WHERE session_id=?;",
                             -1, &up, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(up, 1, category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(up, 2, sid);
        sqlite3_step(up);
        sqlite3_finalize(up);
      }
    } else {
      score = ScoreForCategory(category);
    }

    if (overrideVal == 1) score = (std::max)(score, gRules.threshold);
    if (overrideVal == 0) score = 0;

    if (score < gRules.threshold) continue;
    long long a = ParseIsoToUnixApprox(start ? start : "");
    long long b = ParseIsoToUnixApprox(end && *end ? end : IsoLocalNow());
    if (b <= a) b = a + 1;  // point samples from track_tab → 1s credit
    productive.push_back({a, b});
  }
  sqlite3_finalize(st);
  sqlite3_close(db);

  long long prodSec = MergeSeconds(productive);
  // Optional sleep subtract
  std::string sleepBody = ReadFileUtf8(Join(behaviorDir, L"sleep_windows.json"));
  if (!sleepBody.empty() && sleepBody.find(today) != std::string::npos) {
    // Best-effort: subtract each {"start","end"} if present for today
    std::vector<Interval> sleep;
    size_t p = 0;
    while (true) {
      size_t s1 = sleepBody.find("\"start\"", p);
      if (s1 == std::string::npos) break;
      std::string frag = sleepBody.substr(s1, 120);
      std::string ss = JsonGetStringInObject(frag, "start");
      size_t e1 = sleepBody.find("\"end\"", s1);
      std::string ee;
      if (e1 != std::string::npos) ee = JsonGetStringInObject(sleepBody.substr(e1, 120), "end");
      p = s1 + 7;
      if (ss.find(today) == std::string::npos) continue;
      long long a = ParseIsoToUnixApprox(ss);
      long long b = ParseIsoToUnixApprox(ee);
      if (b > a) sleep.push_back({a, b});
    }
    if (!sleep.empty() && !productive.empty()) {
      // Crude: subtract sleep total from productive (not perfect clip; good enough until P5c)
      long long sleepSec = MergeSeconds(sleep);
      if (prodSec > sleepSec) prodSec -= sleepSec;
      else
        prodSec = 0;
    }
  }

  int prodMin = (int)(prodSec / 60);
  int focusGoal = 180;
  {
    std::string pol = ReadFileUtf8(Join(behaviorDir, L"softland_policy.json"));
    if (pol.empty()) pol = ReadFileUtf8(Join(behaviorDir, L"..\\softland_policy.json"));
    int fromPol = 0;
    if (!pol.empty() && JsonGetIntNear(pol, "daily_focus_minutes", &fromPol) && fromPol > 0)
      focusGoal = fromPol;
  }
  bool goalMet = prodMin >= focusGoal;
  std::ostringstream js;
  js << "{\n"
     << "  \"schema_version\": 1,\n"
     << "  \"local_date\": \"" << today << "\",\n"
     << "  \"updated_at\": \"" << IsoLocalNow() << "\",\n"
     << "  \"productive_minutes\": " << prodMin << ",\n"
     << "  \"productive_seconds\": " << prodSec << ",\n"
     << "  \"threshold\": " << gRules.threshold << ",\n"
     << "  \"daily_focus_minutes\": " << focusGoal << ",\n"
     << "  \"session_count\": " << sessionCount << ",\n"
     << "  \"day_unlocked\": " << (goalMet ? "true" : "false") << ",\n"
     << "  \"goal_met\": " << (goalMet ? "true" : "false") << ",\n"
     << "  \"source\": \"calt_enforcer\",\n"
     << "  \"rules_loaded\": " << (gRules.app.empty() && gRules.domain.empty() ? "false" : "true")
     << "\n"
     << "}\n";
  WriteFileUtf8(Join(behaviorDir, L"day_rollup.json"), js.str());
}
