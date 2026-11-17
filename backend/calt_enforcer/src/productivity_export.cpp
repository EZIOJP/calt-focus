#include "productivity_export.h"

#include "sqlite3.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kMaxExportDays = 366;

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        o += "\\\"";
        break;
      case '\\':
        o += "\\\\";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      case '\t':
        o += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += (char)c;
        }
    }
  }
  return o;
}

std::string CsvEscape(const std::string& s) {
  bool need = false;
  for (char c : s) {
    if (c == '"' || c == ',' || c == '\n' || c == '\r') {
      need = true;
      break;
    }
  }
  if (!need) return s;
  std::string o = "\"";
  for (char c : s) {
    if (c == '"') o += "\"\"";
    else
      o += c;
  }
  o += "\"";
  return o;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& body) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = WriteFile(h, body.data(), (DWORD)body.size(), &written, nullptr);
  CloseHandle(h);
  return ok && written == body.size();
}

std::string ReadFileUtf8(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
    CloseHandle(h);
    return {};
  }
  std::string out((size_t)sz.QuadPart, '\0');
  DWORD read = 0;
  BOOL ok = ReadFile(h, out.data(), (DWORD)sz.QuadPart, &read, nullptr);
  CloseHandle(h);
  if (!ok) return {};
  out.resize(read);
  return out;
}

bool JsonGetStringNear(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
  if (p >= body.size() || body[p] != '"') return false;
  ++p;
  std::string val;
  while (p < body.size() && body[p] != '"') {
    if (body[p] == '\\' && p + 1 < body.size()) {
      val += body[p + 1];
      p += 2;
      continue;
    }
    val += body[p++];
  }
  *out = val;
  return true;
}

bool JsonGetIntNear(const std::string& body, const char* key, int* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
  if (p >= body.size()) return false;
  *out = atoi(body.c_str() + p);
  return true;
}

bool JsonGetDoubleNear(const std::string& body, const char* key, double* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
  if (p >= body.size()) return false;
  if (body.compare(p, 4, "null") == 0) return false;
  *out = atof(body.c_str() + p);
  return true;
}

bool JsonGetBoolNear(const std::string& body, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
  if (body.compare(p, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (body.compare(p, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

std::string TodayLocalYmd() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
  return buf;
}

bool ParseYmd(const std::string& ymd, int* Y, int* M, int* D) {
  if (ymd.size() < 10 || ymd[4] != '-' || ymd[7] != '-') return false;
  *Y = atoi(ymd.substr(0, 4).c_str());
  *M = atoi(ymd.substr(5, 2).c_str());
  *D = atoi(ymd.substr(8, 2).c_str());
  return *Y > 2000 && *M >= 1 && *M <= 12 && *D >= 1 && *D <= 31;
}

std::string AddDaysYmd(const std::string& ymd, int delta) {
  int Y = 0, M = 0, D = 0;
  if (!ParseYmd(ymd, &Y, &M, &D)) return ymd;
  SYSTEMTIME st{};
  st.wYear = (WORD)Y;
  st.wMonth = (WORD)M;
  st.wDay = (WORD)D;
  st.wHour = 12;
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) return ymd;
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  const long long day100ns = 24LL * 60 * 60 * 10000000LL;
  uli.QuadPart = (ULONGLONG)((long long)uli.QuadPart + (long long)delta * day100ns);
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  FILETIME localFt = ft;
  SYSTEMTIME out{};
  if (!FileTimeToSystemTime(&localFt, &out)) return ymd;
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", out.wYear, out.wMonth, out.wDay);
  return buf;
}

const char* WeekdayName(const std::string& ymd) {
  int Y = 0, M = 0, D = 0;
  if (!ParseYmd(ymd, &Y, &M, &D)) return "";
  SYSTEMTIME st{};
  st.wYear = (WORD)Y;
  st.wMonth = (WORD)M;
  st.wDay = (WORD)D;
  st.wHour = 12;
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) return "";
  SYSTEMTIME out{};
  if (!FileTimeToSystemTime(&ft, &out)) return "";
  static const char* k[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
  if (out.wDayOfWeek > 6) return "";
  return k[out.wDayOfWeek];
}

long long ParseIsoToUnixApprox(const std::string& iso) {
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
  SystemTimeToFileTime(&st, &ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return (long long)((uli.QuadPart / 10000000ULL) - 11644473600ULL);
}

std::string DatePrefix(const std::string& iso) {
  if (iso.size() >= 10) return iso.substr(0, 10);
  return {};
}

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

struct DayAgg {
  long long actual_sec = 0;
  long long productive_sec = 0;
  int session_count = 0;
  int block_count = 0;
  long long planned_sec = 0;
};

struct SessionRow {
  std::string session_id;
  std::string date;
  std::string start_at;
  std::string end_at;
  std::string app_name;
  std::string window_title;
  std::string category;
  std::string source;
  int score = 0;
  bool productive = false;
  long long seconds = 0;
  int hour = 0;
};

struct BlockRow {
  std::string date;
  long long id = 0;
  std::string title;
  std::string category;
  std::string start_at;
  std::string end_at;
  long long planned_sec = 0;
  std::string status;
};

/** Flattened life + wearable + body-care mirrors for export (Health UI SoT). */
struct HealthRow {
  std::string date;
  double sleep_hours = 0;
  int sleep_quality = 0;
  int sleep_score = 0;
  int steps = 0;
  int hr_last = 0;
  int stress = 0;
  int exercise_minutes = 0;
  int study_minutes = 0;
  int mood_score = 0;
  int meditation_minutes = 0;
  int outdoor_minutes = 0;
  int calories = 0;
  int distance_m = 0;
  std::string source;
  std::string synced_at;
  bool has_life = false;
  bool has_wearable = false;
  // bodycare_sidecar day rollup
  bool has_body_care = false;
  double body_attention_avg = 0;
  double body_posture_avg = 0;
  double body_present_minutes = 0;
  double body_slouch_minutes = 0;
  double body_distance_avg = 0;
  int body_samples = 0;
};

HealthRow LoadHealthForDay(const std::wstring& behaviorDir, const std::string& day) {
  HealthRow row;
  row.date = day;
  const std::wstring dayW(day.begin(), day.end());
  std::string life = ReadFileUtf8(behaviorDir + L"\\life_days\\" + dayW + L".json");
  if (life.empty()) {
    std::string todayMirror = ReadFileUtf8(behaviorDir + L"\\life_today.json");
    std::string mirrorDay;
    if (!todayMirror.empty() && JsonGetStringNear(todayMirror, "date", &mirrorDay) &&
        mirrorDay == day) {
      life = todayMirror;
    }
  }
  std::string wear = ReadFileUtf8(behaviorDir + L"\\wearable_days\\" + dayW + L".json");
  if (wear.empty()) {
    std::string todayWear = ReadFileUtf8(behaviorDir + L"\\wearable_day.json");
    std::string mirrorDay;
    if (!todayWear.empty()) {
      if (!JsonGetStringNear(todayWear, "local_date", &mirrorDay))
        JsonGetStringNear(todayWear, "date", &mirrorDay);
      if (mirrorDay == day) wear = todayWear;
    }
  }

  if (!life.empty()) {
    row.has_life = true;
    JsonGetDoubleNear(life, "sleep_hours", &row.sleep_hours);
    JsonGetIntNear(life, "sleep_quality", &row.sleep_quality);
    JsonGetIntNear(life, "steps", &row.steps);
    JsonGetIntNear(life, "hr_last", &row.hr_last);
    JsonGetIntNear(life, "stress_level", &row.stress);
    JsonGetIntNear(life, "exercise_minutes", &row.exercise_minutes);
    JsonGetIntNear(life, "study_minutes", &row.study_minutes);
    JsonGetIntNear(life, "mood_score", &row.mood_score);
    JsonGetIntNear(life, "meditation_minutes", &row.meditation_minutes);
    JsonGetIntNear(life, "outdoor_minutes", &row.outdoor_minutes);
    JsonGetStringNear(life, "source", &row.source);
    JsonGetStringNear(life, "synced_at", &row.synced_at);
  }
  if (!wear.empty()) {
    row.has_wearable = true;
    double sh = 0;
    if (JsonGetDoubleNear(wear, "sleep_hours", &sh) && sh > 0) row.sleep_hours = sh;
    int sc = 0;
    if (JsonGetIntNear(wear, "sleep_score", &sc) && sc > 0) row.sleep_score = sc;
    int steps = 0;
    if (JsonGetIntNear(wear, "steps", &steps) && steps > 0) row.steps = steps;
    int hr = 0;
    if (JsonGetIntNear(wear, "hr_last", &hr) && hr > 0) row.hr_last = hr;
    int stress = 0;
    // Wearable uses 0–100 stress; prefer over life 1–5 when present.
    if (JsonGetIntNear(wear, "stress", &stress) && stress > 0) row.stress = stress;
    JsonGetIntNear(wear, "calories", &row.calories);
    JsonGetIntNear(wear, "distance_m", &row.distance_m);
    std::string src;
    if (JsonGetStringNear(wear, "source", &src) && !src.empty()) row.source = src;
    std::string synced;
    if (JsonGetStringNear(wear, "synced_at", &synced) && !synced.empty()) row.synced_at = synced;
  }

  std::string body = ReadFileUtf8(behaviorDir + L"\\body_care_days\\" + dayW + L".json");
  if (body.empty()) {
    std::string latest = ReadFileUtf8(behaviorDir + L"\\body_care.json");
    std::string mirrorDay;
    if (!latest.empty()) {
      // Prefer nested day.date when present.
      if (JsonGetStringNear(latest, "date", &mirrorDay) && mirrorDay == day) {
        body = latest;
      }
    }
  }
  if (!body.empty()) {
    double attn = 0, post = 0, present = 0, slouch = 0, dist = 0;
    int samples = 0;
    bool any = false;
    if (JsonGetDoubleNear(body, "attention_pct_avg", &attn)) {
      row.body_attention_avg = attn;
      any = true;
    }
    if (JsonGetDoubleNear(body, "posture_score_avg", &post)) {
      row.body_posture_avg = post;
      any = true;
    }
    if (JsonGetDoubleNear(body, "present_minutes", &present)) {
      row.body_present_minutes = present;
      any = true;
    }
    if (JsonGetDoubleNear(body, "slouch_minutes", &slouch)) {
      row.body_slouch_minutes = slouch;
      any = true;
    }
    if (JsonGetDoubleNear(body, "distance_proxy_avg", &dist)) {
      row.body_distance_avg = dist;
      any = true;
    }
    if (JsonGetIntNear(body, "samples", &samples) && samples > 0) {
      row.body_samples = samples;
      any = true;
    }
    row.has_body_care = any;
  }
  return row;
}

int LoadThreshold(const std::wstring& behaviorDir) {
  int thr = 60;
  std::string body = ReadFileUtf8(behaviorDir + L"\\classify_rules.json");
  if (!body.empty()) JsonGetIntNear(body, "productive_threshold", &thr);
  if (thr <= 0) thr = 60;
  return thr;
}

std::map<std::string, int> LoadScoreMap(const std::wstring& behaviorDir) {
  std::map<std::string, int> scores;
  std::string body = ReadFileUtf8(behaviorDir + L"\\classify_rules.json");
  if (body.empty()) return scores;
  size_t p = body.find("\"category_scores\"");
  if (p == std::string::npos) p = body.find("\"scores\"");
  if (p == std::string::npos) return scores;
  size_t brace = body.find('{', p);
  if (brace == std::string::npos) return scores;
  size_t i = brace + 1;
  while (i < body.size() && body[i] != '}') {
    while (i < body.size() && (body[i] == ' ' || body[i] == ',' || body[i] == '\n' ||
                               body[i] == '\r' || body[i] == '\t'))
      ++i;
    if (i >= body.size() || body[i] == '}') break;
    if (body[i] != '"') break;
    ++i;
    std::string key;
    while (i < body.size() && body[i] != '"') key += body[i++];
    if (i < body.size() && body[i] == '"') ++i;
    while (i < body.size() && body[i] != ':') ++i;
    if (i < body.size() && body[i] == ':') ++i;
    while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
    int val = atoi(body.c_str() + i);
    if (!key.empty()) scores[key] = val;
    while (i < body.size() && body[i] != ',' && body[i] != '}') ++i;
  }
  return scores;
}

int ScoreForCategory(const std::map<std::string, int>& scores, const std::string& cat, int defScore) {
  auto it = scores.find(cat);
  if (it != scores.end()) return it->second;
  return defScore;
}

std::wstring ResolveDb(const std::wstring& behaviorDir, const std::wstring& dbPathIn) {
  if (!dbPathIn.empty() && GetFileAttributesW(dbPathIn.c_str()) != INVALID_FILE_ATTRIBUTES)
    return dbPathIn;
  std::wstring dataDir = behaviorDir;
  size_t slash = dataDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
  std::wstring db = dataDir + L"\\productivity.db";
  if (GetFileAttributesW(db.c_str()) != INVALID_FILE_ATTRIBUTES) return db;
  return dataDir + L"\\vocab_app.db";
}

}  // namespace

std::string ProductivityExportHandle(const std::wstring& behaviorDir, const std::wstring& dbPathIn,
                                     const std::string& payloadJson, std::string* extraOut) {
  int days = 7;
  JsonGetIntNear(payloadJson, "days", &days);
  if (days < 1) days = 1;
  if (days > kMaxExportDays) days = kMaxExportDays;

  std::string endDay;
  JsonGetStringNear(payloadJson, "end_day", &endDay);
  if (endDay.size() < 10) endDay = TodayLocalYmd();
  endDay = endDay.substr(0, 10);

  std::string format = "json";
  JsonGetStringNear(payloadJson, "format", &format);
  if (format != "csv" && format != "csv_wide") format = "json";

  bool skipEmpty = true;
  JsonGetBoolNear(payloadJson, "skip_empty", &skipEmpty);
  bool productiveOnly = false;
  JsonGetBoolNear(payloadJson, "productive_only", &productiveOnly);
  // Burnout analysis needs the aggregates, not a potentially multi-megabyte
  // stream of raw window titles. Keep that stream opt-in for forensic exports.
  bool analysisOnly = false;
  JsonGetBoolNear(payloadJson, "analysis_only", &analysisOnly);

  const std::string startDay = AddDaysYmd(endDay, -(days - 1));
  const std::string endExclusive = AddDaysYmd(endDay, 1);

  const int threshold = LoadThreshold(behaviorDir);
  const auto scoreMap = LoadScoreMap(behaviorDir);
  int defaultScore = 35;
  {
    std::string body = ReadFileUtf8(behaviorDir + L"\\classify_rules.json");
    JsonGetIntNear(body, "default_score", &defaultScore);
  }

  std::wstring dbPath = ResolveDb(behaviorDir, dbPathIn);
  sqlite3* db = nullptr;
  if (sqlite3_open16(dbPath.c_str(), &db) != SQLITE_OK || !db) {
    return "export_db_open_failed";
  }
  sqlite3_busy_timeout(db, 3000);

  std::map<std::string, DayAgg> byDay;
  for (std::string d = startDay; d <= endDay; d = AddDaysYmd(d, 1)) {
    byDay[d] = DayAgg{};
  }

  std::vector<SessionRow> sessions;
  {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT session_id, app_name, window_title, category, start_time, end_time, "
        "override_productive, IFNULL(source,'') "
        "FROM tracked_sessions "
        "WHERE start_time >= ? AND start_time < ? "
        "AND IFNULL(source,'') IN ('desktop_tracker','selftracker','extension','native');";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, startDay.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(st, 2, endExclusive.c_str(), -1, SQLITE_TRANSIENT);
      while (sqlite3_step(st) == SQLITE_ROW) {
        const char* sid = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        const char* app = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        const char* start = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        const char* end = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
        int overrideCol = sqlite3_column_type(st, 6);
        int overrideVal = overrideCol == SQLITE_NULL ? -1 : sqlite3_column_int(st, 6);
        const char* source = reinterpret_cast<const char*>(sqlite3_column_text(st, 7));

        SessionRow row;
        row.session_id = sid ? sid : "";
        row.start_at = start ? start : "";
        row.end_at = end && *end ? end : "";
        row.app_name = app ? app : "";
        row.window_title = title ? title : "";
        row.category = cat && *cat ? cat : "Uncategorized";
        row.source = source ? source : "";
        row.date = DatePrefix(row.start_at);
        if (row.date.empty() || row.date < startDay || row.date > endDay) continue;

        long long a = ParseIsoToUnixApprox(row.start_at);
        long long b = row.end_at.empty() ? a + 1 : ParseIsoToUnixApprox(row.end_at);
        if (b <= a) b = a + 1;
        row.seconds = b - a;
        if (row.start_at.size() >= 13) row.hour = atoi(row.start_at.substr(11, 2).c_str());

        row.score = ScoreForCategory(scoreMap, row.category, defaultScore);
        if (overrideVal == 1) row.score = (std::max)(row.score, threshold);
        if (overrideVal == 0) row.score = 0;
        {
          std::string appLow = row.app_name;
          for (auto& c : appLow) c = (char)tolower((unsigned char)c);
          if (appLow == "cursor.exe" || appLow == "cursor") row.score = 0;
        }
        row.productive = row.score >= threshold;
        if (productiveOnly && !row.productive) continue;

        DayAgg& day = byDay[row.date];
        day.actual_sec += row.seconds;
        day.session_count += 1;
        if (row.productive) day.productive_sec += row.seconds;
        sessions.push_back(std::move(row));
      }
      sqlite3_finalize(st);
    }
  }

  std::vector<BlockRow> blocks;
  {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id, title, category, start_at, end_at, IFNULL(planned_minutes,0), "
        "IFNULL(status,'') FROM planner_blocks "
        "WHERE start_at >= ? AND start_at < ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, startDay.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(st, 2, endExclusive.c_str(), -1, SQLITE_TRANSIENT);
      while (sqlite3_step(st) == SQLITE_ROW) {
        BlockRow row;
        row.id = sqlite3_column_int64(st, 0);
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        const char* start = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        const char* end = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        int plannedMin = sqlite3_column_int(st, 5);
        const char* status = reinterpret_cast<const char*>(sqlite3_column_text(st, 6));
        row.title = title ? title : "";
        row.category = cat ? cat : "";
        row.start_at = start ? start : "";
        row.end_at = end ? end : "";
        row.status = status ? status : "";
        row.date = DatePrefix(row.start_at);
        if (row.date.empty() || row.date < startDay || row.date > endDay) continue;
        row.planned_sec = (long long)plannedMin * 60;
        if (row.planned_sec <= 0) {
          long long a = ParseIsoToUnixApprox(row.start_at);
          long long b = ParseIsoToUnixApprox(row.end_at);
          if (b > a) row.planned_sec = b - a;
        }
        DayAgg& day = byDay[row.date];
        day.block_count += 1;
        day.planned_sec += row.planned_sec;
        blocks.push_back(std::move(row));
      }
      sqlite3_finalize(st);
    }
  }
  sqlite3_close(db);

  std::vector<std::string> dayOrder;
  for (std::string d = startDay; d <= endDay; d = AddDaysYmd(d, 1)) dayOrder.push_back(d);

  std::vector<HealthRow> healthRows;
  healthRows.reserve(dayOrder.size());
  for (const auto& d : dayOrder) {
    HealthRow h = LoadHealthForDay(behaviorDir, d);
    if (h.has_life || h.has_wearable || h.has_body_care) healthRows.push_back(std::move(h));
  }

  auto isEmptyDay = [](const DayAgg& a) {
    return a.session_count == 0 && a.actual_sec <= 0 && a.productive_sec <= 0 &&
           a.block_count == 0 && a.planned_sec <= 0;
  };

  long long sumProd = 0, sumAct = 0;
  int sumSessions = 0, daysWithData = 0;
  for (const auto& d : dayOrder) {
    const DayAgg& a = byDay[d];
    if (isEmptyDay(a)) continue;
    ++daysWithData;
    sumProd += a.productive_sec;
    sumAct += a.actual_sec;
    sumSessions += a.session_count;
  }

  std::ostringstream js;
  js << "{\n"
     << "  \"export_version\": \"focus-1.1\",\n"
     << "  \"source\": \"calt_enforcer\",\n"
     << "  \"exported_at\": \"" << IsoLocalNow() << "\",\n"
     << "  \"range\": {\"start\":\"" << startDay << "\",\"end\":\"" << endDay
     << "\",\"days\":" << days << "},\n"
     << "  \"summary\": {"
     << "\"productive_minutes\":" << (sumProd / 60) << ","
     << "\"actual_minutes\":" << (sumAct / 60) << ","
     << "\"session_count\":" << sumSessions << ","
     << "\"days_with_data\":" << daysWithData << "},\n"
     << "  \"by_day\": [\n";
  bool firstDay = true;
  for (const auto& d : dayOrder) {
    const DayAgg& a = byDay[d];
    if (skipEmpty && isEmptyDay(a)) continue;
    if (!firstDay) js << ",\n";
    firstDay = false;
    js << "    {\"date\":\"" << d << "\",\"weekday\":\"" << WeekdayName(d) << "\","
       << "\"planned_minutes\":" << (a.planned_sec / 60) << ","
       << "\"actual_minutes\":" << (a.actual_sec / 60) << ","
       << "\"productive_minutes\":" << (a.productive_sec / 60) << ","
       << "\"session_count\":" << a.session_count << ","
       << "\"block_count\":" << a.block_count << "}";
  }
  js << "\n  ],\n  \"tables\": {\n    \"day_metrics\": [\n";
  firstDay = true;
  for (const auto& d : dayOrder) {
    const DayAgg& a = byDay[d];
    if (skipEmpty && isEmptyDay(a)) continue;
    if (!firstDay) js << ",\n";
    firstDay = false;
    js << "      {\"date\":\"" << d << "\",\"weekday\":\"" << WeekdayName(d) << "\","
       << "\"planned_minutes\":" << (a.planned_sec / 60) << ","
       << "\"actual_minutes\":" << (a.actual_sec / 60) << ","
       << "\"productive_minutes\":" << (a.productive_sec / 60) << ","
       << "\"session_count\":" << a.session_count << ","
       << "\"block_count\":" << a.block_count << "}";
  }
  js << "\n    ],\n    \"sessions\": [\n";
  if (!analysisOnly) {
    for (size_t i = 0; i < sessions.size(); ++i) {
      const SessionRow& r = sessions[i];
      if (i) js << ",\n";
      js << "      {\"session_id\":\"" << JsonEscape(r.session_id) << "\",\"date\":\"" << r.date
         << "\",\"weekday\":\"" << WeekdayName(r.date) << "\",\"start_at\":\""
         << JsonEscape(r.start_at) << "\",\"end_at\":\"" << JsonEscape(r.end_at)
         << "\",\"minutes\":" << (r.seconds / 60) << ",\"hour\":" << r.hour << ",\"app_name\":\""
         << JsonEscape(r.app_name) << "\",\"window_title\":\"" << JsonEscape(r.window_title)
         << "\",\"category\":\"" << JsonEscape(r.category) << "\",\"source\":\""
         << JsonEscape(r.source) << "\",\"productivity_score\":" << r.score
         << ",\"is_productive\":" << (r.productive ? "true" : "false") << "}";
    }
  }
  js << "\n    ],\n    \"planned_blocks\": [\n";
  for (size_t i = 0; i < blocks.size(); ++i) {
    const BlockRow& r = blocks[i];
    if (i) js << ",\n";
    js << "      {\"date\":\"" << r.date << "\",\"weekday\":\"" << WeekdayName(r.date)
       << "\",\"block_id\":" << r.id << ",\"title\":\"" << JsonEscape(r.title)
       << "\",\"category\":\"" << JsonEscape(r.category) << "\",\"start_at\":\""
       << JsonEscape(r.start_at) << "\",\"end_at\":\"" << JsonEscape(r.end_at)
       << "\",\"planned_minutes\":" << (r.planned_sec / 60) << ",\"status\":\""
       << JsonEscape(r.status) << "\"}";
  }
  js << "\n    ],\n    \"health_days\": [\n";
  for (size_t i = 0; i < healthRows.size(); ++i) {
    const HealthRow& r = healthRows[i];
    if (i) js << ",\n";
    char sleepBuf[32];
    snprintf(sleepBuf, sizeof(sleepBuf), "%.2f", r.sleep_hours);
    js << "      {\"date\":\"" << r.date << "\",\"weekday\":\"" << WeekdayName(r.date)
       << "\",\"sleep_hours\":" << sleepBuf << ",\"sleep_quality\":" << r.sleep_quality
       << ",\"sleep_score\":" << r.sleep_score << ",\"steps\":" << r.steps
       << ",\"hr_last\":" << r.hr_last << ",\"stress\":" << r.stress
       << ",\"exercise_minutes\":" << r.exercise_minutes
       << ",\"study_minutes\":" << r.study_minutes << ",\"mood_score\":" << r.mood_score
       << ",\"meditation_minutes\":" << r.meditation_minutes
       << ",\"outdoor_minutes\":" << r.outdoor_minutes << ",\"calories\":" << r.calories
       << ",\"distance_m\":" << r.distance_m << ",\"source\":\"" << JsonEscape(r.source)
       << "\",\"synced_at\":\"" << JsonEscape(r.synced_at)
       << "\",\"has_life\":" << (r.has_life ? "true" : "false")
       << ",\"has_wearable\":" << (r.has_wearable ? "true" : "false")
       << ",\"has_body_care\":" << (r.has_body_care ? "true" : "false")
       << ",\"body_attention_avg\":" << r.body_attention_avg
       << ",\"body_posture_avg\":" << r.body_posture_avg
       << ",\"body_present_minutes\":" << r.body_present_minutes
       << ",\"body_slouch_minutes\":" << r.body_slouch_minutes
       << ",\"body_distance_avg\":" << r.body_distance_avg
       << ",\"body_samples\":" << r.body_samples << "}";
  }
  js << "\n    ]\n  }\n}\n";

  const std::wstring jsonPath = behaviorDir + L"\\productivity_export.json";
  if (!WriteFileUtf8(jsonPath, js.str())) return "export_write_json_failed";

  // Long CSV (multi-table) — same markers Study uses.
  std::ostringstream csv;
  csv << "# CALT productivity export - tidy long format\n";
  csv << "# version=focus-1.1,exported_at=" << IsoLocalNow() << ",range_start=" << startDay
      << ",range_end=" << endDay << "\n";
  csv << "\n# table:day_metrics\n";
  csv << "date,weekday,planned_minutes,actual_minutes,productive_minutes,session_count,block_count\n";
  for (const auto& d : dayOrder) {
    const DayAgg& a = byDay[d];
    if (skipEmpty && isEmptyDay(a)) continue;
    csv << d << "," << WeekdayName(d) << "," << (a.planned_sec / 60) << "," << (a.actual_sec / 60)
        << "," << (a.productive_sec / 60) << "," << a.session_count << "," << a.block_count
        << "\n";
  }
  csv << "\n# table:sessions\n";
  csv << "session_id,date,weekday,start_at,end_at,minutes,hour,app_name,window_title,category,"
         "source,productivity_score,is_productive\n";
  if (!analysisOnly) {
    for (const SessionRow& r : sessions) {
      csv << CsvEscape(r.session_id) << "," << r.date << "," << WeekdayName(r.date) << ","
          << CsvEscape(r.start_at) << "," << CsvEscape(r.end_at) << "," << (r.seconds / 60) << ","
          << r.hour << "," << CsvEscape(r.app_name) << "," << CsvEscape(r.window_title) << ","
          << CsvEscape(r.category) << "," << CsvEscape(r.source) << "," << r.score << ","
          << (r.productive ? "1" : "0") << "\n";
    }
  }
  csv << "\n# table:planned_blocks\n";
  csv << "date,weekday,block_id,title,category,start_at,end_at,planned_minutes,status\n";
  for (const BlockRow& r : blocks) {
    csv << r.date << "," << WeekdayName(r.date) << "," << r.id << "," << CsvEscape(r.title) << ","
        << CsvEscape(r.category) << "," << CsvEscape(r.start_at) << "," << CsvEscape(r.end_at)
        << "," << (r.planned_sec / 60) << "," << CsvEscape(r.status) << "\n";
  }
  csv << "\n# table:health_days\n";
  csv << "date,weekday,sleep_hours,sleep_quality,sleep_score,steps,hr_last,stress,"
         "exercise_minutes,study_minutes,mood_score,meditation_minutes,outdoor_minutes,"
         "calories,distance_m,source,synced_at,has_life,has_wearable,has_body_care,"
         "body_attention_avg,body_posture_avg,body_present_minutes,body_slouch_minutes,"
         "body_distance_avg,body_samples\n";
  for (const HealthRow& r : healthRows) {
    char sleepBuf[32];
    snprintf(sleepBuf, sizeof(sleepBuf), "%.2f", r.sleep_hours);
    csv << r.date << "," << WeekdayName(r.date) << "," << sleepBuf << "," << r.sleep_quality << ","
        << r.sleep_score << "," << r.steps << "," << r.hr_last << "," << r.stress << ","
        << r.exercise_minutes << "," << r.study_minutes << "," << r.mood_score << ","
        << r.meditation_minutes << "," << r.outdoor_minutes << "," << r.calories << ","
        << r.distance_m << "," << CsvEscape(r.source) << "," << CsvEscape(r.synced_at) << ","
        << (r.has_life ? "1" : "0") << "," << (r.has_wearable ? "1" : "0") << ","
        << (r.has_body_care ? "1" : "0") << "," << r.body_attention_avg << ","
        << r.body_posture_avg << "," << r.body_present_minutes << "," << r.body_slouch_minutes
        << "," << r.body_distance_avg << "," << r.body_samples << "\n";
  }

  // Wide CSV: one row per day (simple) + health columns when present.
  std::map<std::string, HealthRow> healthByDay;
  for (const HealthRow& h : healthRows) healthByDay[h.date] = h;
  std::ostringstream wide;
  wide << "date,weekday,planned_minutes,actual_minutes,productive_minutes,session_count,block_"
          "count,sleep_hours,steps,hr_last,stress,calories\n";
  for (const auto& d : dayOrder) {
    const DayAgg& a = byDay[d];
    auto hit = healthByDay.find(d);
    const bool hasHealth = hit != healthByDay.end();
    if (skipEmpty && isEmptyDay(a) && !hasHealth) continue;
    char sleepBuf[32];
    snprintf(sleepBuf, sizeof(sleepBuf), "%.2f", hasHealth ? hit->second.sleep_hours : 0.0);
    wide << d << "," << WeekdayName(d) << "," << (a.planned_sec / 60) << "," << (a.actual_sec / 60)
         << "," << (a.productive_sec / 60) << "," << a.session_count << "," << a.block_count << ","
         << sleepBuf << "," << (hasHealth ? hit->second.steps : 0) << ","
         << (hasHealth ? hit->second.hr_last : 0) << "," << (hasHealth ? hit->second.stress : 0)
         << "," << (hasHealth ? hit->second.calories : 0) << "\n";
  }

  const std::wstring csvPath = behaviorDir + L"\\productivity_export.csv";
  const std::string& csvBody = (format == "csv_wide") ? wide.str() : csv.str();
  if (!WriteFileUtf8(csvPath, csvBody)) return "export_write_csv_failed";

  const std::string stamp = endDay;
  std::string stampCompact;
  for (char c : stamp)
    if (c != '-') stampCompact += c;
  std::string downloadName;
  if (format == "json")
    downloadName = "productivity-" + std::to_string(days) + "d-" + stampCompact + ".json";
  else if (format == "csv_wide")
    downloadName = "productivity-" + std::to_string(days) + "d-" + stampCompact + "-wide.csv";
  else
    downloadName = "productivity-" + std::to_string(days) + "d-" + stampCompact + "-long.csv";

  if (extraOut) {
    std::ostringstream o;
    o << ",\"export\":{"
      << "\"json_file\":\"productivity_export.json\","
      << "\"csv_file\":\"productivity_export.csv\","
      << "\"format\":\"" << format << "\","
      << "\"analysis_only\":" << (analysisOnly ? "true" : "false") << ","
      << "\"download_name\":\"" << JsonEscape(downloadName) << "\","
      << "\"range\":{\"start\":\"" << startDay << "\",\"end\":\"" << endDay << "\",\"days\":" << days
      << "},"
      << "\"summary\":{\"productive_minutes\":" << (sumProd / 60)
      << ",\"actual_minutes\":" << (sumAct / 60) << ",\"session_count\":" << sumSessions
      << ",\"days_with_data\":" << daysWithData << "}"
      << "}";
    *extraOut = o.str();
  }
  return "";
}

std::string ProductivityExportPresenceHandle(const std::wstring& behaviorDir,
                                             const std::wstring& dbPathIn,
                                             const std::string& payloadJson,
                                             std::string* extraOut) {
  std::string start, end;
  JsonGetStringNear(payloadJson, "start", &start);
  JsonGetStringNear(payloadJson, "end", &end);
  if (start.size() < 10 || end.size() < 10) return "export_presence_bad_range";
  start = start.substr(0, 10);
  end = end.substr(0, 10);
  if (end < start) std::swap(start, end);
  // Cap window
  if (AddDaysYmd(start, kMaxExportDays - 1) < end) start = AddDaysYmd(end, -(kMaxExportDays - 1));
  const std::string endExclusive = AddDaysYmd(end, 1);

  std::wstring dbPath = ResolveDb(behaviorDir, dbPathIn);
  sqlite3* db = nullptr;
  if (sqlite3_open16(dbPath.c_str(), &db) != SQLITE_OK || !db) return "export_db_open_failed";
  sqlite3_busy_timeout(db, 3000);

  std::map<std::string, bool> marked;
  auto mark = [&](const std::string& iso) {
    std::string d = DatePrefix(iso);
    if (d.size() == 10 && d >= start && d <= end) marked[d] = true;
  };

  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT start_time FROM tracked_sessions WHERE start_time >= ? AND "
                               "start_time < ?;",
                           -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, start.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(st, 2, endExclusive.c_str(), -1, SQLITE_TRANSIENT);
      while (sqlite3_step(st) == SQLITE_ROW) {
        const char* s = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        if (s) mark(s);
      }
      sqlite3_finalize(st);
    }
  }
  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT start_at FROM planner_blocks WHERE start_at >= ? AND start_at < "
                           "?;",
                           -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, start.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(st, 2, endExclusive.c_str(), -1, SQLITE_TRANSIENT);
      while (sqlite3_step(st) == SQLITE_ROW) {
        const char* s = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        if (s) mark(s);
      }
      sqlite3_finalize(st);
    }
  }
  sqlite3_close(db);

  if (extraOut) {
    std::ostringstream o;
    o << ",\"start\":\"" << start << "\",\"end\":\"" << end << "\",\"days\":[";
    bool first = true;
    for (const auto& kv : marked) {
      if (!first) o << ",";
      first = false;
      o << "\"" << kv.first << "\"";
    }
    o << "],\"count\":" << marked.size();
    *extraOut = o.str();
  }
  return "";
}
