#include "day_loop.h"

#include "plan_gate.h"
#include "policy_db.h"
#include "productivity_store.h"
#include "softland_publish.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sqlite3.h"

namespace {

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

std::string LocalDate() { return ProductivityLocalDate(); }

std::string AddMinutesLocalIso(int minutes) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  FILETIME ft;
  SystemTimeToFileTime(&st, &ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  uli.QuadPart += (ULONGLONG)minutes * 60ULL * 10000000ULL;
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  SYSTEMTIME outSt;
  FileTimeToSystemTime(&ft, &outSt);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", outSt.wYear, outSt.wMonth, outSt.wDay,
           outSt.wHour, outSt.wMinute, outSt.wSecond);
  return buf;
}

std::string EndOfLocalDayIso() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT23:59:59", st.wYear, st.wMonth, st.wDay);
  return buf;
}

std::wstring BehaviorToDbPath(const std::wstring& behaviorDir) {
  std::wstring dataDir = behaviorDir;
  size_t slash = dataDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
  std::wstring dbPath = dataDir + L"\\productivity.db";
  if (GetFileAttributesW(dbPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    dbPath = dataDir + L"\\vocab_app.db";
  return dbPath;
}

bool ArmHardBlock(const std::wstring& behaviorDir, bool* armedOut) {
  std::wstring dbPath = BehaviorToDbPath(behaviorDir);
  std::wstring polPath = behaviorDir + L"\\enforcer_policy.json";
  EnforcerSnapshot cur;
  std::string loadErr;
  LoadEnforcerSnapshot(dbPath, cur, loadErr);
  cur.armed = true;
  cur.locked = true;
  if (cur.lock_mode.empty()) cur.lock_mode = "none";

  // Seed kill list once (Arm ON with empty exes kills nothing).
  static const wchar_t* kSeed[] = {
      L"cursor.exe",
      L"steam.exe",
      L"steamwebhelper.exe",
      L"discord.exe",
      L"discordptb.exe",
      L"discordcanary.exe",
      L"spotify.exe",
      L"epicgameslauncher.exe",
      L"riotclientservices.exe",
      L"leagueclient.exe",
      L"valorant.exe",
      L"robloxplayerbeta.exe",
      L"battle.net.exe",
      L"netflix.exe",
      nullptr,
  };
  auto hasExe = [&](const wchar_t* name) {
    for (const auto& e : cur.exes) {
      if (_wcsicmp(e.c_str(), name) == 0) return true;
    }
    return false;
  };
  if (cur.exes.empty()) {
    for (int i = 0; kSeed[i]; ++i) cur.exes.push_back(kSeed[i]);
  } else if (!hasExe(L"cursor.exe")) {
    cur.exes.push_back(L"cursor.exe");  // study-temp until goal; cleared on free
  }

  if (!WriteEnforcerPolicyJson(polPath, cur, cur.anti_tamper, false)) return false;
  SyncEnforcerRuntimeSqlite(dbPath, cur);
  if (armedOut) *armedOut = true;
  return true;
}

/** Arm stays off until Confirm couples SoftLand + Arm for today. */
bool DisarmHardBlock(const std::wstring& behaviorDir) {
  std::wstring dbPath = BehaviorToDbPath(behaviorDir);
  std::wstring polPath = behaviorDir + L"\\enforcer_policy.json";
  EnforcerSnapshot cur;
  std::string loadErr;
  LoadEnforcerSnapshot(dbPath, cur, loadErr);
  if (!cur.armed && !cur.locked) return true;
  cur.armed = false;
  cur.locked = false;
  if (!WriteEnforcerPolicyJson(polPath, cur, cur.anti_tamper, false)) return false;
  SyncEnforcerRuntimeSqlite(dbPath, cur);
  return true;
}

/** Drop cursor.exe from kill list after goal free (keep games/social armed). */
bool ClearStudyTempKills(const std::wstring& behaviorDir) {
  std::wstring dbPath = BehaviorToDbPath(behaviorDir);
  std::wstring polPath = behaviorDir + L"\\enforcer_policy.json";
  EnforcerSnapshot cur;
  std::string loadErr;
  LoadEnforcerSnapshot(dbPath, cur, loadErr);
  std::vector<std::wstring> next;
  for (const auto& e : cur.exes) {
    if (_wcsicmp(e.c_str(), L"cursor.exe") == 0) continue;
    next.push_back(e);
  }
  if (next.size() == cur.exes.size()) return true;
  cur.exes = std::move(next);
  if (!WriteEnforcerPolicyJson(polPath, cur, cur.anti_tamper, false)) return false;
  SyncEnforcerRuntimeSqlite(dbPath, cur);
  return true;
}

std::string JsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (unsigned char c : in) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof(b), "\\u%04x", c);
          out += b;
        } else
          out.push_back((char)c);
    }
  }
  return out;
}

bool JsonGetString(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size() || body[i] != '"') return false;
  ++i;
  std::string val;
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
      val.push_back(body[i + 1]);
      i += 2;
      continue;
    }
    val.push_back(body[i++]);
  }
  *out = val;
  return true;
}

bool JsonGetBool(const std::string& body, const char* key, bool* out) {
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

bool JsonGetInt(const std::string& body, const char* key, int* out) {
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

bool ConfirmMatches(const std::string& payload, const char* expected) {
  std::string got;
  if (!JsonGetString(payload, "confirm", &got)) return false;
  size_t a = got.find_first_not_of(" \t\r\n");
  size_t b = got.find_last_not_of(" \t\r\n");
  if (a == std::string::npos) return false;
  std::string trimmed = got.substr(a, b - a + 1);
  for (auto& c : trimmed) c = (char)toupper((unsigned char)c);
  return trimmed == expected;
}

std::string ReadFileUtf8(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 4 * 1024 * 1024) {
    CloseHandle(h);
    return {};
  }
  std::string out(static_cast<size_t>(sz.QuadPart), '\0');
  DWORD n = 0;
  BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &n, nullptr);
  CloseHandle(h);
  if (!ok) return {};
  out.resize(n);
  return out;
}

bool IsFreeCategory(const std::string& cat) {
  std::string c = cat;
  for (auto& ch : c) ch = (char)tolower((unsigned char)ch);
  return c == "break" || c == "free" || c == "reward" || c == "leisure" || c == "rest" ||
         c == "downtime" || c == "food" || c == "sleep" || c == "commute";
}

int PlannedMinutesToday(int userId) {
  if (!gDb) return 0;
  const std::string day = LocalDate();
  const std::string from = day + "T00:00:00";
  const std::string to = day + "T23:59:59";
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT planned_minutes, category FROM planner_blocks "
      "WHERE user_id=? AND start_at<=? AND end_at>=? AND status NOT IN ('cancelled','rolled')";
  int sum = 0;
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return 0;
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, to.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, from.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(st) == SQLITE_ROW) {
    int mins = sqlite3_column_int(st, 0);
    const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    if (cat && IsFreeCategory(cat)) continue;
    if (mins > 0) sum += mins;
  }
  sqlite3_finalize(st);
  return sum;
}

int TrackedProductiveMinutes(const std::wstring& behaviorDir) {
  std::string body = ReadFileUtf8(behaviorDir + L"\\day_rollup.json");
  if (body.empty()) return 0;
  int mins = 0;
  if (JsonGetInt(body, "productive_minutes", &mins)) return mins < 0 ? 0 : mins;
  return 0;
}

void TaskCounts(const std::string& date, int userId, int* total, int* done) {
  *total = 0;
  *done = 0;
  if (!gDb) return;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT COUNT(*), COALESCE(SUM(CASE WHEN done=1 THEN 1 ELSE 0 END),0) "
                         "FROM productivity_day_tasks WHERE user_id=? AND task_date=?;",
                         -1, &st, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, date.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) == SQLITE_ROW) {
    *total = sqlite3_column_int(st, 0);
    *done = sqlite3_column_int(st, 1);
  }
  sqlite3_finalize(st);
}

std::string SaveSoftland(ProductivitySoftland& s, const std::wstring& behaviorDir) {
  s.updated_at = IsoLocalNow();
  ProductivityApplyCacheToDocument(s);
  if (!ProductivitySaveSoftland(s)) return "store_save_failed";
  ProductivityBumpSeq();
  PublishSoftlandMirror(behaviorDir, s);
  return "";
}

bool SetRuntimeString(ProductivitySoftland& s, const char* key, const std::string& valOrEmpty,
                      bool asNullIfEmpty) {
  std::string raw;
  if (asNullIfEmpty && valOrEmpty.empty())
    raw = "null";
  else
    raw = std::string("\"") + JsonEscape(valOrEmpty) + "\"";
  // Prefer nested under runtime object via Replace on document — key may live in runtime.
  if (ProductivityReplaceJsonValue(s.document_json, key, raw)) return true;
  // Insert into runtime object if missing
  size_t rt = s.document_json.find("\"runtime\"");
  if (rt == std::string::npos) return false;
  size_t brace = s.document_json.find('{', rt);
  if (brace == std::string::npos) return false;
  std::string insert = std::string("\"") + key + "\": " + raw + ", ";
  s.document_json.insert(brace + 1, insert);
  return true;
}

bool SetRuntimeBool(ProductivitySoftland& s, const char* key, bool v) {
  if (ProductivityReplaceJsonValue(s.document_json, key, v ? "true" : "false")) return true;
  size_t rt = s.document_json.find("\"runtime\"");
  if (rt == std::string::npos) return false;
  size_t brace = s.document_json.find('{', rt);
  if (brace == std::string::npos) return false;
  s.document_json.insert(brace + 1, std::string("\"") + key + "\": " + (v ? "true" : "false") + ", ");
  return true;
}

int ParseHm(const std::string& hm) {
  if (hm.size() < 4) return -1;
  int h = atoi(hm.c_str());
  size_t colon = hm.find(':');
  if (colon == std::string::npos) return -1;
  int m = atoi(hm.c_str() + colon + 1);
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

bool IsoStillActive(const std::string& until, const std::string& now) {
  if (until.empty() || until == "null") return false;
  std::string u = until.size() > 19 ? until.substr(0, 19) : until;
  std::string n = now.size() > 19 ? now.substr(0, 19) : now;
  return u > n;
}

int DailyFocusMinutesFromDoc(const std::string& doc) {
  int focus = 0;
  JsonGetInt(doc, "daily_focus_minutes", &focus);
  if (focus <= 0) focus = 180;
  return focus;
}

bool RewriteGoalsObject(ProductivitySoftland& s, bool bible, bool planConfirmed, bool goalMet,
                        int focusMin, const std::string& planDate, const std::string& bibleDate) {
  std::string goalsRaw = std::string("{\"daily_focus_minutes\":") + std::to_string(focusMin) +
                         ",\"goal_met\":" + (goalMet ? "true" : "false") +
                         ",\"bible_done\":" + (bible ? "true" : "false") +
                         ",\"bible_done_for_date\":" +
                         (bibleDate.empty() ? "null" : ("\"" + bibleDate + "\"")) +
                         ",\"plan_confirmed_for_date\":" +
                         (planDate.empty() ? "null" : ("\"" + planDate + "\"")) +
                         ",\"plan_confirmed\":" + (planConfirmed ? "true" : "false") + "}";
  return ProductivityReplaceJsonValue(s.document_json, "goals", goalsRaw);
}

bool GoalsDoneToday(const std::string& doc, const char* doneKey, const char* dateKey,
                    const std::string& today) {
  bool done = false;
  JsonGetBool(doc, doneKey, &done);
  if (!done) return false;
  std::string d;
  JsonGetString(doc, dateKey, &d);
  return d == today;
}

}  // namespace

void DayLoopEnsureSchema() {
  if (!gDb) return;
  sqlite3_exec(gDb,
               "CREATE TABLE IF NOT EXISTS productivity_day_tasks ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  user_id INTEGER NOT NULL DEFAULT 1,"
               "  task_date TEXT NOT NULL,"
               "  title TEXT NOT NULL,"
               "  done INTEGER NOT NULL DEFAULT 0,"
               "  sort_order INTEGER NOT NULL DEFAULT 0,"
               "  created_at TEXT NOT NULL"
               ");"
               "CREATE INDEX IF NOT EXISTS idx_day_tasks_date "
               "ON productivity_day_tasks(user_id, task_date);",
               nullptr, nullptr, nullptr);
}

std::string DayLoopTaskListJson(const std::string& dateYmdOrEmpty, int userId) {
  DayLoopEnsureSchema();
  const std::string date = dateYmdOrEmpty.empty() ? LocalDate() : dateYmdOrEmpty;
  if (!gDb) return "[]";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT id, title, done, sort_order FROM productivity_day_tasks "
                         "WHERE user_id=? AND task_date=? ORDER BY sort_order, id;",
                         -1, &st, nullptr) != SQLITE_OK)
    return "[]";
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, date.c_str(), -1, SQLITE_TRANSIENT);
  std::string out = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    if (!first) out += ",";
    first = false;
    long long id = sqlite3_column_int64(st, 0);
    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    int done = sqlite3_column_int(st, 2);
    int sort = sqlite3_column_int(st, 3);
    out += "{\"id\":" + std::to_string(id) + ",\"title\":\"" + JsonEscape(title ? title : "") +
           "\",\"done\":" + (done ? "true" : "false") + ",\"sort_order\":" + std::to_string(sort) +
           ",\"task_date\":\"" + date + "\"}";
  }
  sqlite3_finalize(st);
  out += "]";
  return out;
}

bool DayLoopTaskUpsert(const std::string& payloadJson, int userId, std::string* outTaskJson) {
  DayLoopEnsureSchema();
  if (!gDb) return false;
  long long id = 0;
  {
    std::string idStr;
    if (JsonGetString(payloadJson, "id", &idStr) && !idStr.empty()) id = atoll(idStr.c_str());
    int idInt = 0;
    if (JsonGetInt(payloadJson, "id", &idInt) && idInt > 0) id = idInt;
  }
  std::string title, date;
  JsonGetString(payloadJson, "title", &title);
  JsonGetString(payloadJson, "task_date", &date);
  if (date.empty()) date = LocalDate();
  if (title.empty()) return false;
  bool done = false;
  JsonGetBool(payloadJson, "done", &done);
  int sort = 0;
  JsonGetInt(payloadJson, "sort_order", &sort);
  const std::string now = IsoLocalNow();

  if (id > 0) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(gDb,
                           "UPDATE productivity_day_tasks SET title=?, done=?, sort_order=?, task_date=? "
                           "WHERE id=? AND user_id=?;",
                           -1, &st, nullptr) != SQLITE_OK)
      return false;
    sqlite3_bind_text(st, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, done ? 1 : 0);
    sqlite3_bind_int(st, 3, sort);
    sqlite3_bind_text(st, 4, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, id);
    sqlite3_bind_int(st, 6, userId);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
  } else {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(gDb,
                           "INSERT INTO productivity_day_tasks "
                           "(user_id, task_date, title, done, sort_order, created_at) "
                           "VALUES (?,?,?,?,?,?);",
                           -1, &st, nullptr) != SQLITE_OK)
      return false;
    sqlite3_bind_int(st, 1, userId);
    sqlite3_bind_text(st, 2, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, done ? 1 : 0);
    sqlite3_bind_int(st, 5, sort);
    sqlite3_bind_text(st, 6, now.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
    id = sqlite3_last_insert_rowid(gDb);
  }
  if (outTaskJson) {
    *outTaskJson = "{\"id\":" + std::to_string(id) + ",\"title\":\"" + JsonEscape(title) +
                   "\",\"done\":" + (done ? "true" : "false") + ",\"sort_order\":" +
                   std::to_string(sort) + ",\"task_date\":\"" + date + "\"}";
  }
  ProductivityBumpSeq();
  return true;
}

bool DayLoopTaskSetDone(long long id, bool done, int userId) {
  DayLoopEnsureSchema();
  if (!gDb || id <= 0) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb, "UPDATE productivity_day_tasks SET done=? WHERE id=? AND user_id=?;", -1,
                         &st, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_int(st, 1, done ? 1 : 0);
  sqlite3_bind_int64(st, 2, id);
  sqlite3_bind_int(st, 3, userId);
  bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(gDb) > 0;
  sqlite3_finalize(st);
  if (ok) ProductivityBumpSeq();
  return ok;
}

bool DayLoopTaskDelete(long long id, int userId) {
  DayLoopEnsureSchema();
  if (!gDb || id <= 0) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb, "DELETE FROM productivity_day_tasks WHERE id=? AND user_id=?;", -1, &st,
                         nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(gDb) > 0;
  sqlite3_finalize(st);
  if (ok) ProductivityBumpSeq();
  return ok;
}

std::string DayLoopConfirmPlan(const std::wstring& behaviorDir, std::string* extraOut) {
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return "store_load_failed";
  const std::string today = LocalDate();

  if (!GoalsDoneToday(s.document_json, "bible_done", "bible_done_for_date", today))
    return "bible_required";

  // Require a real today plan before SoftLand+Arm couple — no empty confirm.
  if (PlannedMinutesToday(1) <= 0) return "plan_required";

  int focus = DailyFocusMinutesFromDoc(s.document_json);
  if (!RewriteGoalsObject(s, true, true, false, focus, today, today)) return "policy_key_missing";

  // Study day: SoftLand + Arm both on (engines stay separate; Confirm couples activation).
  s.softland_enabled = true;
  std::string err = SaveSoftland(s, behaviorDir);
  if (!err.empty()) return err;

  bool armed = false;
  if (!ArmHardBlock(behaviorDir, &armed)) return "arm_write_failed";
  ProductivityBumpSeq();

  if (extraOut)
    *extraOut = ",\"plan_confirmed\":true,\"plan_confirmed_for_date\":\"" + today +
                "\",\"softland_enabled\":true,\"armed\":" + (armed ? "true" : "false");
  return "";
}

std::string DayLoopSnapshot(const std::wstring& behaviorDir, const std::wstring& /*dbPath*/,
                            std::string* extraOut) {
  DayLoopEnsureSchema();
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return "store_load_failed";
  const std::string today = LocalDate();
  int planned = PlannedMinutesToday(1);
  int tracked = TrackedProductiveMinutes(behaviorDir);
  int total = 0, done = 0;
  TaskCounts(today, 1, &total, &done);
  bool checkboxesOk = (total == 0) || (done >= total && total > 0);
  int need = planned > 0 ? (planned + 1) / 2 : 0;
  bool timeOk = planned > 0 && tracked >= need;
  bool freeGranted = false;
  std::string grantedDate;
  JsonGetString(s.document_json, "closeout_free_granted_date", &grantedDate);
  if (grantedDate == today) freeGranted = true;
  std::string bedtime, wake, emergency;
  JsonGetString(s.document_json, "bedtime_hm", &bedtime);
  JsonGetString(s.document_json, "wake_hm", &wake);
  JsonGetString(s.document_json, "emergency_until", &emergency);
  bool bedtimeActive = false;
  JsonGetBool(s.document_json, "bedtime_active", &bedtimeActive);
  bool planConfirmed =
      GoalsDoneToday(s.document_json, "plan_confirmed", "plan_confirmed_for_date", today);
  bool bibleDone = GoalsDoneToday(s.document_json, "bible_done", "bible_done_for_date", today);

  std::string tasks = DayLoopTaskListJson(today, 1);
  std::string freeJs = s.free_until.empty() ? "null" : ("\"" + JsonEscape(s.free_until) + "\"");
  std::string bedJs = bedtime.empty() ? "null" : ("\"" + JsonEscape(bedtime) + "\"");
  std::string wakeJs = wake.empty() ? "null" : ("\"" + JsonEscape(wake) + "\"");
  std::string emJs = emergency.empty() ? "null" : ("\"" + JsonEscape(emergency) + "\"");

  if (extraOut) {
    *extraOut =
        ",\"loop\":{"
        "\"date\":\"" +
        today + "\",\"planned_minutes\":" + std::to_string(planned) +
        ",\"tracked_productive_minutes\":" + std::to_string(tracked) +
        ",\"threshold_minutes\":" + std::to_string(need) +
        ",\"tasks_total\":" + std::to_string(total) + ",\"tasks_done\":" + std::to_string(done) +
        ",\"checkboxes_ok\":" + (checkboxesOk ? "true" : "false") +
        ",\"time_ok\":" + (timeOk ? "true" : "false") +
        ",\"dual_gate_ok\":" + ((checkboxesOk && timeOk) ? "true" : "false") +
        ",\"closeout_free_granted\":" + (freeGranted ? "true" : "false") +
        ",\"free_until\":" + freeJs + ",\"bedtime_hm\":" + bedJs + ",\"wake_hm\":" + wakeJs +
        ",\"bedtime_active\":" + (bedtimeActive ? "true" : "false") +
        ",\"emergency_until\":" + emJs +
        ",\"bible_done\":" + (bibleDone ? "true" : "false") +
        ",\"plan_confirmed\":" + (planConfirmed ? "true" : "false") +
        ",\"softland_enabled\":" + (s.softland_enabled ? "true" : "false") +
        ",\"tasks\":" + tasks + "}";
  }
  return "";
}

std::string DayLoopEvaluateClose(const std::wstring& behaviorDir, const std::wstring& dbPath,
                                 std::string* extraOut) {
  std::string snapExtra;
  std::string err = DayLoopSnapshot(behaviorDir, dbPath, &snapExtra);
  if (!err.empty()) return err;

  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return "store_load_failed";
  const std::string today = LocalDate();
  std::string grantedDate;
  JsonGetString(s.document_json, "closeout_free_granted_date", &grantedDate);
  if (grantedDate == today) {
    if (extraOut) *extraOut = snapExtra + ",\"granted\":false,\"reason\":\"already_granted\"";
    return "";
  }

  int planned = PlannedMinutesToday(1);
  int tracked = TrackedProductiveMinutes(behaviorDir);
  int total = 0, done = 0;
  TaskCounts(today, 1, &total, &done);
  bool checkboxesOk = (total == 0) || (done >= total);
  int need = planned > 0 ? (planned + 1) / 2 : 0;
  bool timeOk = planned > 0 && tracked >= need;
  if (!checkboxesOk || !timeOk) {
    if (extraOut)
      *extraOut = snapExtra + ",\"granted\":false,\"reason\":\"dual_gate_failed\"";
    return "";
  }

  s.free_until = AddMinutesLocalIso(60);
  SetRuntimeString(s, "closeout_free_granted_date", today, false);
  err = SaveSoftland(s, behaviorDir);
  if (!err.empty()) return err;
  ProductivityLedgerAdd("earn", 3600, "closeout dual-gate 1h free", "day_loop");
  if (extraOut)
    *extraOut = snapExtra + ",\"granted\":true,\"free_until\":\"" + JsonEscape(s.free_until) + "\"";
  return "";
}

std::string DayLoopSetBedtime(const std::wstring& behaviorDir, const std::string& payload) {
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return "store_load_failed";
  std::string bed, wake;
  JsonGetString(payload, "bedtime_hm", &bed);
  JsonGetString(payload, "wake_hm", &wake);
  if (bed.empty() || ParseHm(bed) < 0) return "bad_payload";
  if (!wake.empty() && ParseHm(wake) < 0) return "bad_payload";
  SetRuntimeString(s, "bedtime_hm", bed, false);
  if (!wake.empty())
    SetRuntimeString(s, "wake_hm", wake, false);
  else
    SetRuntimeString(s, "wake_hm", "", true);
  return SaveSoftland(s, behaviorDir);
}

bool DayLoopMaybeGrantGoalFree(const std::wstring& behaviorDir) {
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return false;
  if (!s.softland_enabled) return false;

  const std::string today = LocalDate();
  if (!GoalsDoneToday(s.document_json, "plan_confirmed", "plan_confirmed_for_date", today))
    return false;

  std::string granted;
  JsonGetString(s.document_json, "goal_free_granted_date", &granted);
  if (granted == today) return false;

  int focus = DailyFocusMinutesFromDoc(s.document_json);
  int tracked = TrackedProductiveMinutes(behaviorDir);
  if (tracked < focus) return false;

  bool bible = GoalsDoneToday(s.document_json, "bible_done", "bible_done_for_date", today);
  std::string bibleDate = bible ? today : "";
  if (!RewriteGoalsObject(s, bible, true, true, focus, today, bibleDate)) return false;
  s.free_until = EndOfLocalDayIso();
  SetRuntimeString(s, "goal_free_granted_date", today, false);
  if (!SaveSoftland(s, behaviorDir).empty()) return false;
  DayLoopClearStudyTempKills(behaviorDir);  // drop cursor.exe; keep games/social armed
  ProductivityLedgerAdd("earn", 0, "goal_met free until eod", "day_loop");
  return true;
}

bool DayLoopBibleDoneToday(const std::string& softlandDocumentJson) {
  return GoalsDoneToday(softlandDocumentJson, "bible_done", "bible_done_for_date", LocalDate());
}

bool DayLoopPlanConfirmedToday(const std::string& softlandDocumentJson) {
  return GoalsDoneToday(softlandDocumentJson, "plan_confirmed", "plan_confirmed_for_date",
                        LocalDate());
}

bool DayLoopClearStudyTempKills(const std::wstring& behaviorDir) {
  return ClearStudyTempKills(behaviorDir);
}

bool DayLoopTick(const std::wstring& behaviorDir, const std::wstring& dbPath) {
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return false;
  bool changed = false;
  const std::string now = IsoLocalNow();
  const std::string today = LocalDate();

  // Midnight / new calendar day: clear sticky bible/plan so morning ritual returns.
  {
    bool bibleSticky = false;
    bool planSticky = false;
    JsonGetBool(s.document_json, "bible_done", &bibleSticky);
    JsonGetBool(s.document_json, "plan_confirmed", &planSticky);
    bool bibleToday =
        GoalsDoneToday(s.document_json, "bible_done", "bible_done_for_date", today);
    bool planToday =
        GoalsDoneToday(s.document_json, "plan_confirmed", "plan_confirmed_for_date", today);
    bool goalMet = false;
    JsonGetBool(s.document_json, "goal_met", &goalMet);
    if ((bibleSticky && !bibleToday) || (planSticky && !planToday) || (goalMet && !planToday)) {
      int focus = DailyFocusMinutesFromDoc(s.document_json);
      if (RewriteGoalsObject(s, false, false, false, focus, "", "")) changed = true;
      // Stale free / goal grant from a prior calendar day must not skip morning.
      std::string goalGrant;
      JsonGetString(s.document_json, "goal_free_granted_date", &goalGrant);
      if (!goalGrant.empty() && goalGrant != today) {
        SetRuntimeString(s, "goal_free_granted_date", "", true);
        changed = true;
      }
      std::string closeGrant;
      JsonGetString(s.document_json, "closeout_free_granted_date", &closeGrant);
      if (!closeGrant.empty() && closeGrant != today) {
        SetRuntimeString(s, "closeout_free_granted_date", "", true);
        changed = true;
      }
      if (!s.free_until.empty()) {
        // free_until dated before today → clear so leftover EOD free doesn't skip ritual.
        std::string fu = s.free_until.size() >= 10 ? s.free_until.substr(0, 10) : "";
        if (!fu.empty() && fu < today) {
          s.free_until.clear();
          changed = true;
        }
      }
      if (s.reward_day_active) {
        s.reward_day_active = false;
        changed = true;
      }
    }

    // Legitimate morning gate: SoftLand ON (sites → morning_bible/plan), Arm OFF until Confirm.
    if (!planToday) {
      if (!s.softland_enabled) {
        s.softland_enabled = true;
        changed = true;
      }
      DisarmHardBlock(behaviorDir);
    }
  }

  std::string emergency;
  JsonGetString(s.document_json, "emergency_until", &emergency);
  if (!emergency.empty() && !IsoStillActive(emergency, now)) {
    SetRuntimeString(s, "emergency_until", "", true);
    changed = true;
  }

  std::string bed, wake;
  JsonGetString(s.document_json, "bedtime_hm", &bed);
  JsonGetString(s.document_json, "wake_hm", &wake);
  bool wasActive = false;
  JsonGetBool(s.document_json, "bedtime_active", &wasActive);
  bool active = false;
  if (!bed.empty()) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    int nowM = st.wHour * 60 + st.wMinute;
    int bedM = ParseHm(bed);
    int wakeM = wake.empty() ? 6 * 60 : ParseHm(wake);
    if (bedM >= 0) {
      if (wakeM < 0) wakeM = 6 * 60;
      // Overnight window: bedtime 23:00 wake 06:00 → active if now>=bed OR now<wake
      if (bedM > wakeM)
        active = nowM >= bedM || nowM < wakeM;
      else
        active = nowM >= bedM && nowM < wakeM;
    }
  }
  if (active != wasActive) {
    SetRuntimeBool(s, "bedtime_active", active);
    changed = true;
  }

  if (changed) {
    s.updated_at = IsoLocalNow();
    ProductivityApplyCacheToDocument(s);
    ProductivitySaveSoftland(s);
    ProductivityBumpSeq();
    PublishSoftlandMirror(behaviorDir, s);
  }

  DayLoopMaybeGrantGoalFree(behaviorDir);

  // Opportunistic close-out grant (idempotent)
  std::string extra;
  DayLoopEvaluateClose(behaviorDir, dbPath, &extra);
  (void)extra;
  return changed;
}

std::string DayLoopEmergencyWinddown(const std::wstring& behaviorDir, const std::string& payload) {
  if (!ConfirmMatches(payload, "EMERGENCY")) return "confirm_required";
  int minutes = 30;
  JsonGetInt(payload, "minutes", &minutes);
  if (minutes < 5) minutes = 5;
  if (minutes > 120) minutes = 120;
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return "store_load_failed";
  std::string until = AddMinutesLocalIso(minutes);
  SetRuntimeString(s, "emergency_until", until, false);
  return SaveSoftland(s, behaviorDir);
}

std::string DayLoopImportFromDate(const std::string& payload, int userId, std::string* extraOut) {
  (void)payload;
  (void)userId;
  (void)extraOut;
  // Product rule: unfinished yesterday plans do not carry into today.
  return "carry_disabled";
}
