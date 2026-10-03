#include "productivity_store.h"
#include "sqlite3.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

sqlite3* gDb = nullptr;

namespace {

std::wstring gDbPath;

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring DirOf(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::string ReadFileUtf8(const std::wstring& path) {
  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, path.c_str(), L"rb");
#else
  f = _wfopen(path.c_str(), L"rb");
#endif
  if (!f) return {};
  std::string body;
  char buf[4096];
  while (size_t n = fread(buf, 1, sizeof(buf), f)) body.append(buf, n);
  fclose(f);
  if (!body.empty() && (unsigned char)body[0] == 0xEF && body.size() >= 3 &&
      (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF) {
    body.erase(0, 3);
  }
  return body;
}

bool JsonBoolNear(const std::string& body, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n'))
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

bool JsonStringNear(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n'))
    ++i;
  if (i < body.size() && body.compare(i, 4, "null") == 0) {
    *out = "";
    return true;
  }
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

bool JsonIntNear(const std::string& body, const char* key, int* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size() || !(isdigit((unsigned char)body[i]) || body[i] == '-')) return false;
  *out = atoi(body.c_str() + i);
  return true;
}

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

bool ReplaceJsonBool(std::string& doc, const char* key, bool v) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = doc.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = doc.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < doc.size() && (doc[i] == ' ' || doc[i] == '\t')) ++i;
  const char* rep = v ? "true" : "false";
  size_t len = 0;
  if (doc.compare(i, 4, "true") == 0)
    len = 4;
  else if (doc.compare(i, 5, "false") == 0)
    len = 5;
  else
    return false;
  doc.replace(i, len, rep);
  return true;
}

bool ReplaceJsonStringOrNull(std::string& doc, const char* key, const std::string& val) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = doc.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = doc.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < doc.size() && (doc[i] == ' ' || doc[i] == '\t' || doc[i] == '\r' || doc[i] == '\n'))
    ++i;
  size_t end = i;
  if (doc.compare(i, 4, "null") == 0) {
    end = i + 4;
  } else if (doc[i] == '"') {
    end = i + 1;
    while (end < doc.size() && doc[end] != '"') {
      if (doc[end] == '\\' && end + 1 < doc.size()) end += 2;
      else
        ++end;
    }
    if (end < doc.size()) ++end;
  } else
    return false;
  std::string rep = val.empty() ? "null" : ("\"" + val + "\"");
  doc.replace(i, end - i, rep);
  return true;
}

/** Wall-clock prefix of an ISO stamp — Python writes an offset, C++ does not. */
std::string IsoWall(const std::string& iso) {
  return iso.size() >= 19 ? iso.substr(0, 19) : iso;
}

std::string JsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20)
          out += ' ';
        else
          out += c;
    }
  }
  return out;
}

bool Exec(const char* sql) {
  if (!gDb) return false;
  char* err = nullptr;
  int rc = sqlite3_exec(gDb, sql, nullptr, nullptr, &err);
  if (err) sqlite3_free(err);
  return rc == SQLITE_OK;
}

std::string LocalDateStr() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
  return buf;
}

/** Monday-start week key, local time. Day passes are a per-week quota. */
std::string WeekStartStr() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  int back = (st.wDayOfWeek == 0) ? 6 : (st.wDayOfWeek - 1);  // wDayOfWeek: 0=Sunday
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) return LocalDateStr();
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  uli.QuadPart -= (ULONGLONG)back * 24ULL * 3600ULL * 10000000ULL;
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  SYSTEMTIME out;
  if (!FileTimeToSystemTime(&ft, &out)) return LocalDateStr();
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", out.wYear, out.wMonth, out.wDay);
  return buf;
}

/** Strings from a JSON array, keeping only "YYYY-MM-DD" tokens. */
void ParseDateArrayNear(const std::string& body, const char* key,
                        std::vector<std::string>& out) {
  out.clear();
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return;
  size_t lb = body.find('[', p);
  size_t rb = (lb == std::string::npos) ? std::string::npos : body.find(']', lb);
  if (lb == std::string::npos || rb == std::string::npos) return;
  for (size_t i = lb + 1; i < rb;) {
    if (body[i] != '"') {
      ++i;
      continue;
    }
    size_t j = body.find('"', i + 1);
    if (j == std::string::npos || j > rb) break;
    std::string tok = body.substr(i + 1, j - i - 1);
    if (tok.size() == 10 && tok[4] == '-' && tok[7] == '-') out.push_back(tok);
    i = j + 1;
  }
}

/** Single-column COUNT/int query with one optional text bind. */
int ScalarInt(const char* sql, const std::string* bind1, const char* bind2) {
  if (!gDb) return 0;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return 0;
  int idx = 1;
  if (bind1) sqlite3_bind_text(st, idx++, bind1->c_str(), -1, SQLITE_TRANSIENT);
  if (bind2) sqlite3_bind_text(st, idx++, bind2, -1, SQLITE_STATIC);
  int n = 0;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n;
}

}  // namespace

void ProductivityRefreshCacheFromDocument(ProductivitySoftland& s) {
  JsonBoolNear(s.document_json, "softland_enabled", &s.softland_enabled);
  JsonStringNear(s.document_json, "free_until", &s.free_until);
  JsonStringNear(s.document_json, "incubation_until", &s.incubation_until);
  JsonBoolNear(s.document_json, "reward_day_active", &s.reward_day_active);
  JsonIntNear(s.document_json, "earned_ledger_seconds", &s.earned_ledger_seconds);
  JsonIntNear(s.document_json, "schema_version", &s.schema_version);
  JsonStringNear(s.document_json, "updated_at", &s.updated_at);
}

bool ProductivityApplyCacheToDocument(ProductivitySoftland& s) {
  if (s.document_json.empty()) return false;
  ReplaceJsonBool(s.document_json, "softland_enabled", s.softland_enabled);
  ReplaceJsonStringOrNull(s.document_json, "free_until", s.free_until);
  ReplaceJsonStringOrNull(s.document_json, "incubation_until", s.incubation_until);
  ReplaceJsonBool(s.document_json, "reward_day_active", s.reward_day_active);
  ReplaceJsonStringOrNull(s.document_json, "updated_at", s.updated_at.empty() ? IsoLocalNow() : s.updated_at);
  // earned_ledger_seconds — simple replace if present
  {
    std::string needle = "\"earned_ledger_seconds\"";
    size_t p = s.document_json.find(needle);
    if (p != std::string::npos) {
      size_t colon = s.document_json.find(':', p + needle.size());
      if (colon != std::string::npos) {
        size_t i = colon + 1;
        while (i < s.document_json.size() &&
               (s.document_json[i] == ' ' || s.document_json[i] == '\t'))
          ++i;
        size_t j = i;
        while (j < s.document_json.size() &&
               (isdigit((unsigned char)s.document_json[j]) || s.document_json[j] == '-'))
          ++j;
        s.document_json.replace(i, j - i, std::to_string(s.earned_ledger_seconds));
      }
    }
  }
  return true;
}

bool ProductivityReplaceJsonValue(std::string& doc, const char* key,
                                  const std::string& rawValueJson) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = doc.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = doc.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < doc.size() && isspace((unsigned char)doc[i])) ++i;
  if (i >= doc.size()) return false;

  size_t end = i;
  if (doc[i] == '{' || doc[i] == '[') {
    char open = doc[i];
    char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool inStr = false;
    for (end = i; end < doc.size(); ++end) {
      char c = doc[end];
      if (inStr) {
        if (c == '\\') {
          ++end;
        } else if (c == '"') {
          inStr = false;
        }
        continue;
      }
      if (c == '"')
        inStr = true;
      else if (c == open)
        ++depth;
      else if (c == close && --depth == 0) {
        ++end;
        break;
      }
    }
    if (depth != 0) return false;
  } else if (doc[i] == '"') {
    end = i + 1;
    while (end < doc.size() && doc[end] != '"') {
      if (doc[end] == '\\' && end + 1 < doc.size())
        end += 2;
      else
        ++end;
    }
    if (end < doc.size()) ++end;
  } else {
    while (end < doc.size() && doc[end] != ',' && doc[end] != '}' && doc[end] != ']') ++end;
    while (end > i && isspace((unsigned char)doc[end - 1])) --end;
  }
  doc.replace(i, end - i, rawValueJson);
  return true;
}

bool ProductivityImportIfStale(const std::wstring& softlandJsonPath) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  std::string rowUpdated;
  if (sqlite3_prepare_v2(gDb, "SELECT updated_at FROM productivity_softland WHERE id=1;", -1, &st,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  bool hasRow = sqlite3_step(st) == SQLITE_ROW;
  if (hasRow) {
    const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    rowUpdated = p ? p : "";
  }
  sqlite3_finalize(st);
  if (!hasRow) return false;

  // A Python writer (reward day, legacy Settings) may have edited the mirror
  // while the gateway was down or in parallel. Newer wall clock wins.
  std::string fileBody = ReadFileUtf8(softlandJsonPath);
  if (fileBody.empty()) return false;
  std::string fileUpdated;
  if (!JsonStringNear(fileBody, "updated_at", &fileUpdated) || fileUpdated.empty()) return false;
  if (IsoWall(fileUpdated) <= IsoWall(rowUpdated)) return false;

  if (sqlite3_prepare_v2(gDb,
                         "UPDATE productivity_softland SET document_json=?, updated_at=? WHERE id=1;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(st, 1, fileBody.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, fileUpdated.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

bool ProductivityDuePending(const std::string& nowIsoLocal,
                            std::vector<ProductivityPendingRow>& out) {
  out.clear();
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT id, op, payload_json FROM productivity_pending_changes "
                         "WHERE status='pending' AND substr(apply_after,1,19) <= ? "
                         "ORDER BY id ASC;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  std::string wall = IsoWall(nowIsoLocal);
  sqlite3_bind_text(st, 1, wall.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(st) == SQLITE_ROW) {
    ProductivityPendingRow row;
    row.id = sqlite3_column_int64(st, 0);
    const char* op = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    const char* pl = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    row.op = op ? op : "";
    row.payload_json = pl ? pl : "{}";
    out.push_back(row);
  }
  sqlite3_finalize(st);
  return true;
}

bool ProductivityMarkPending(long long id, const char* status) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb, "UPDATE productivity_pending_changes SET status=? WHERE id=?;", -1,
                         &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(st, 1, status, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 2, id);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

bool ProductivityInsertPending(const std::string& applyAfterIso, const std::string& op,
                               const std::string& payloadJson) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT INTO productivity_pending_changes (created_at, apply_after, op, "
                         "payload_json, status) VALUES (?, ?, ?, ?, 'pending');",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  std::string now = IsoLocalNow();
  sqlite3_bind_text(st, 1, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, applyAfterIso.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, op.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, payloadJson.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

bool ProductivityLedgerAdd(const char* kind, int seconds, const std::string& note,
                           const char* source) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT INTO productivity_ledger (ts, kind, seconds, note, source) "
                         "VALUES (?, ?, ?, ?, ?);",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  std::string now = IsoLocalNow();
  sqlite3_bind_text(st, 1, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, kind, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, seconds);
  sqlite3_bind_text(st, 4, note.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, source ? source : "gateway", -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

std::string ProductivityLedgerRecentJson(int limit) {
  if (!gDb) return "[]";
  if (limit <= 0 || limit > 500) limit = 50;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT ts, kind, seconds, note, source FROM productivity_ledger "
                         "ORDER BY id DESC LIMIT ?;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return "[]";
  }
  sqlite3_bind_int(st, 1, limit);
  std::string out = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    auto col = [&](int i) -> std::string {
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
      return p ? p : "";
    };
    if (!first) out += ",";
    first = false;
    out += "{\"ts\":\"" + JsonEscape(col(0)) + "\",\"kind\":\"" + JsonEscape(col(1)) +
           "\",\"seconds\":" + std::to_string(sqlite3_column_int(st, 2)) + ",\"note\":\"" +
           JsonEscape(col(3)) + "\",\"source\":\"" + JsonEscape(col(4)) + "\"}";
  }
  sqlite3_finalize(st);
  out += "]";
  return out;
}

std::string ProductivityPendingListJson(int limit) {
  if (!gDb) return "[]";
  if (limit <= 0 || limit > 200) limit = 20;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT id, created_at, apply_after, op, payload_json, status FROM "
                         "productivity_pending_changes WHERE status='pending' ORDER BY id ASC "
                         "LIMIT ?;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return "[]";
  }
  sqlite3_bind_int(st, 1, limit);
  std::string out = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    auto col = [&](int i) -> std::string {
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
      return p ? p : "";
    };
    if (!first) out += ",";
    first = false;
    out += "{\"id\":" + std::to_string(sqlite3_column_int64(st, 0)) + ",\"created_at\":\"" +
           JsonEscape(col(1)) + "\",\"apply_after\":\"" + JsonEscape(col(2)) + "\",\"op\":\"" +
           JsonEscape(col(3)) + "\",\"payload\":" + (col(4).empty() ? "{}" : col(4)) +
           ",\"status\":\"" + JsonEscape(col(5)) + "\"}";
  }
  sqlite3_finalize(st);
  out += "]";
  return out;
}

std::wstring ProductivityBehaviorDirFromDb(const std::wstring& dbPath) {
  return Join(DirOf(dbPath), L"behavior");
}

bool ProductivityStoreOpen(const std::wstring& dbPath) {
  ProductivityStoreClose();
  gDbPath = dbPath;
  std::string path = Narrow(dbPath);
  if (sqlite3_open_v2(path.c_str(), &gDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
      SQLITE_OK) {
    if (gDb) {
      sqlite3_close(gDb);
      gDb = nullptr;
    }
    return false;
  }
  sqlite3_busy_timeout(gDb, 5000);
  sqlite3_exec(gDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  return true;
}

void ProductivityStoreClose() {
  if (gDb) {
    sqlite3_close(gDb);
    gDb = nullptr;
  }
}

bool ProductivityMigrateAndImport(const std::wstring& softlandJsonPath) {
  if (!gDb) return false;
  const char* ddl =
      "CREATE TABLE IF NOT EXISTS productivity_softland ("
      "  id INTEGER PRIMARY KEY CHECK (id = 1),"
      "  document_json TEXT NOT NULL,"
      "  updated_at TEXT NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_ledger ("
      "  id INTEGER PRIMARY KEY,"
      "  ts TEXT NOT NULL,"
      "  kind TEXT NOT NULL,"
      "  seconds INTEGER NOT NULL,"
      "  note TEXT,"
      "  source TEXT"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_pending_changes ("
      "  id INTEGER PRIMARY KEY,"
      "  created_at TEXT NOT NULL,"
      "  apply_after TEXT NOT NULL,"
      "  op TEXT NOT NULL,"
      "  payload_json TEXT NOT NULL,"
      "  status TEXT NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_gateway_meta ("
      "  id INTEGER PRIMARY KEY CHECK (id = 1),"
      "  gateway_seq INTEGER NOT NULL,"
      "  last_publish_at TEXT"
      ");"
      // P5a — unlock accounting owned by the enforcer, not the study webapp.
      "CREATE TABLE IF NOT EXISTS productivity_day_passes ("
      "  local_date TEXT PRIMARY KEY,"
      "  week_start TEXT NOT NULL,"
      "  granted_at TEXT NOT NULL,"
      "  source TEXT NOT NULL DEFAULT 'gateway'"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_reward_credits ("
      "  local_date TEXT NOT NULL,"
      "  kind TEXT NOT NULL,"  // 'qualified' | 'used'
      "  recorded_at TEXT NOT NULL,"
      "  PRIMARY KEY (local_date, kind)"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_reward_meta ("
      "  id INTEGER PRIMARY KEY CHECK (id = 1),"
      "  granted INTEGER NOT NULL DEFAULT 0"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_day_events ("
      "  local_date TEXT NOT NULL,"
      "  event TEXT NOT NULL,"
      "  at TEXT NOT NULL,"
      "  earned_seconds INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY (local_date, event)"
      ");"
      // Phase 6a — planner owned by enforcer (same names as Python SoT tables).
      "CREATE TABLE IF NOT EXISTS planner_blocks ("
      "  id INTEGER PRIMARY KEY,"
      "  user_id INTEGER NOT NULL,"
      "  title VARCHAR NOT NULL,"
      "  category VARCHAR NOT NULL DEFAULT 'study',"
      "  start_at DATETIME NOT NULL,"
      "  end_at DATETIME NOT NULL,"
      "  planned_minutes INTEGER NOT NULL,"
      "  remaining_minutes INTEGER NOT NULL,"
      "  status VARCHAR NOT NULL DEFAULT 'scheduled',"
      "  rolled_from_id INTEGER,"
      "  roll_count INTEGER NOT NULL DEFAULT 0,"
      "  task_id INTEGER,"
      "  color VARCHAR,"
      "  created_at DATETIME"
      ");"
      "CREATE TABLE IF NOT EXISTS planner_routines ("
      "  id INTEGER PRIMARY KEY,"
      "  user_id INTEGER NOT NULL,"
      "  title VARCHAR NOT NULL,"
      "  category VARCHAR NOT NULL DEFAULT 'personal',"
      "  start_time VARCHAR NOT NULL,"
      "  end_time VARCHAR,"
      "  duration_minutes INTEGER,"
      "  days_json TEXT NOT NULL DEFAULT '[\"mon\",\"tue\",\"wed\",\"thu\",\"fri\",\"sat\",\"sun\"]',"
      "  color VARCHAR,"
      "  enabled BOOLEAN NOT NULL DEFAULT 1,"
      "  sort_order INTEGER NOT NULL DEFAULT 0,"
      "  created_at DATETIME"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_planner_meta ("
      "  id INTEGER PRIMARY KEY CHECK (id = 1),"
      "  imported_at TEXT,"
      "  block_count INTEGER NOT NULL DEFAULT 0,"
      "  routine_count INTEGER NOT NULL DEFAULT 0"
      ");";
  if (!Exec(ddl)) return false;
  Exec("INSERT OR IGNORE INTO productivity_gateway_meta (id, gateway_seq, last_publish_at) "
       "VALUES (1, 0, NULL);");
  Exec("INSERT OR IGNORE INTO productivity_reward_meta (id, granted) VALUES (1, 0);");
  ProductivityEnsurePlanner();

  sqlite3_stmt* st = nullptr;
  bool hasRow = false;
  std::string rowUpdated;
  if (sqlite3_prepare_v2(gDb, "SELECT updated_at FROM productivity_softland WHERE id=1;", -1, &st,
                         nullptr) == SQLITE_OK) {
    if (sqlite3_step(st) == SQLITE_ROW) {
      hasRow = true;
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
      rowUpdated = p ? p : "";
    }
    sqlite3_finalize(st);
  }
  if (hasRow) {
    ProductivityImportIfStale(softlandJsonPath);
  } else {
    (void)rowUpdated;

    std::string body = ReadFileUtf8(softlandJsonPath);
    if (body.empty()) {
      body =
          "{\n  \"schema_version\": 1,\n  \"updated_at\": null,\n  \"softland_enabled\": false,\n"
          "  \"site_rules\": {\"allow_extra\": [], \"watch_extra\": [], \"block_extra\": []},\n"
          "  \"schedules\": {\"enabled\": false, \"windows\": []},\n"
          "  \"runtime\": {\"free_until\": null, \"free_after_hm\": null, \"incubation_until\": null,\n"
          "    \"day_pass\": {\"date\": null, \"spent\": false, \"remaining_seconds\": 0},\n"
          "    \"reward_day_active\": false, \"earned_ledger_seconds\": 0},\n"
          "  \"goals\": {\"daily_focus_minutes\": 0, \"goal_met\": false, \"bible_done\": false,\n"
          "    \"bible_done_for_date\": null, \"plan_confirmed_for_date\": null, \"plan_confirmed\": false},\n"
          "  \"mode_flags\": {}\n}\n";
    }
    std::string now = IsoLocalNow();
    if (sqlite3_prepare_v2(gDb,
                           "INSERT INTO productivity_softland (id, document_json, updated_at) "
                           "VALUES (1, ?, ?);",
                           -1, &st, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_text(st, 1, body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, now.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
  }

  // One-time import of Bible JSON unlock history into productivity_* tables.
  // softlandJsonPath is …/data/behavior/softland_policy.json → dataDir is …/data.
  const std::wstring behaviorDir = DirOf(softlandJsonPath);
  const std::wstring dataDir = DirOf(behaviorDir);
  ProductivityImportLegacyUnlockHistory(dataDir);
  return true;
}

bool ProductivityLoadSoftland(ProductivitySoftland& out) {
  out = ProductivitySoftland{};
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT document_json, updated_at FROM productivity_softland WHERE id=1;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  bool ok = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const char* doc = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    const char* upd = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    out.document_json = doc ? doc : "";
    out.updated_at = upd ? upd : "";
    ProductivityRefreshCacheFromDocument(out);
    ok = true;
  }
  sqlite3_finalize(st);
  return ok;
}

bool ProductivitySaveSoftland(const ProductivitySoftland& in) {
  if (!gDb || in.document_json.empty()) return false;
  ProductivitySoftland tmp = in;
  if (tmp.updated_at.empty()) tmp.updated_at = IsoLocalNow();
  ProductivityApplyCacheToDocument(tmp);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "UPDATE productivity_softland SET document_json=?, updated_at=? WHERE id=1;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(st, 1, tmp.document_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, tmp.updated_at.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

unsigned ProductivityBumpSeq() {
  if (!gDb) return 0;
  Exec("UPDATE productivity_gateway_meta SET gateway_seq = gateway_seq + 1 WHERE id=1;");
  ProductivityGatewayMeta m;
  if (!ProductivityLoadMeta(m)) return 0;
  return m.gateway_seq;
}

bool ProductivityLoadMeta(ProductivityGatewayMeta& out) {
  out = ProductivityGatewayMeta{};
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT gateway_seq, last_publish_at FROM productivity_gateway_meta WHERE "
                         "id=1;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  bool ok = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    out.gateway_seq = (unsigned)sqlite3_column_int(st, 0);
    const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    out.last_publish_at = p ? p : "";
    ok = true;
  }
  sqlite3_finalize(st);
  return ok;
}

// ---------------------------------------------------------------------------
// P5a — day passes, reward credits, per-day events
// ---------------------------------------------------------------------------

std::string ProductivityLocalDate() { return LocalDateStr(); }
std::string ProductivityWeekStart() { return WeekStartStr(); }

int ProductivityPassesUsedThisWeek() {
  std::string wk = WeekStartStr();
  return ScalarInt("SELECT COUNT(*) FROM productivity_day_passes WHERE week_start = ?;", &wk,
                   nullptr);
}

bool ProductivityPassGrantedToday() {
  std::string d = LocalDateStr();
  return ScalarInt("SELECT COUNT(*) FROM productivity_day_passes WHERE local_date = ?;", &d,
                   nullptr) > 0;
}

bool ProductivityInsertPass(const std::string& localDate, const std::string& weekStart) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT OR IGNORE INTO productivity_day_passes "
                         "(local_date, week_start, granted_at, source) VALUES (?, ?, ?, 'gateway');",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  std::string now = IsoLocalNow();
  sqlite3_bind_text(st, 1, localDate.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, weekStart.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, now.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

int ProductivityRewardCount(const char* kind) {
  return ScalarInt("SELECT COUNT(*) FROM productivity_reward_credits WHERE kind = ?;", nullptr,
                   kind);
}

int ProductivityRewardGranted() {
  return ScalarInt("SELECT granted FROM productivity_reward_meta WHERE id = 1;", nullptr, nullptr);
}

bool ProductivityRewardSetGranted(int granted) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb, "UPDATE productivity_reward_meta SET granted = ? WHERE id = 1;", -1,
                         &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(st, 1, granted < 0 ? 0 : granted);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

bool ProductivityRewardMark(const char* kind, const std::string& localDate) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT OR IGNORE INTO productivity_reward_credits "
                         "(local_date, kind, recorded_at) VALUES (?, ?, ?);",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  std::string now = IsoLocalNow();
  sqlite3_bind_text(st, 1, localDate.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, kind, -1, SQLITE_STATIC);
  sqlite3_bind_text(st, 3, now.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

bool ProductivityDayEventSeconds(const std::string& localDate, const std::string& event,
                                 int* outSeconds) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT earned_seconds FROM productivity_day_events "
                         "WHERE local_date = ? AND event = ?;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(st, 1, localDate.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, event.c_str(), -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    found = true;
    if (outSeconds) *outSeconds = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return found;
}

int ProductivityDayEarnedSecondsToday() {
  std::string d = LocalDateStr();
  return ScalarInt("SELECT COALESCE(SUM(earned_seconds), 0) FROM productivity_day_events "
                   "WHERE local_date = ?;",
                   &d, nullptr);
}

bool ProductivityInsertDayEvent(const std::string& localDate, const std::string& event,
                                int earnedSeconds) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT OR IGNORE INTO productivity_day_events "
                         "(local_date, event, at, earned_seconds) VALUES (?, ?, ?, ?);",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  std::string now = IsoLocalNow();
  sqlite3_bind_text(st, 1, localDate.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, event.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 4, earnedSeconds);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

int ProductivityIncubationStartsSinceIso(const std::string& sinceIso) {
  return ScalarInt("SELECT COUNT(*) FROM productivity_ledger "
                   "WHERE kind = 'incubation' AND ts >= ?;",
                   &sinceIso, nullptr);
}

void ProductivityImportLegacyUnlockHistory(const std::wstring& dataDir) {
  if (!gDb) return;
  // Import only into empty tables — re-running must not resurrect rows the
  // owner deleted on purpose, and must not double-count history.
  const bool haveRewards =
      ProductivityRewardCount("qualified") > 0 || ProductivityRewardCount("used") > 0;
  const bool havePasses = ScalarInt("SELECT COUNT(*) FROM productivity_day_passes;", nullptr,
                                    nullptr) > 0;
  if (haveRewards && havePasses) return;

  const std::wstring bibleDir = dataDir + L"\\bible";

  if (!haveRewards) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((bibleDir + L"\\reward_days_*.json").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string body = ReadFileUtf8(bibleDir + L"\\" + fd.cFileName);
        if (body.empty()) continue;
        std::vector<std::string> dates;
        ParseDateArrayNear(body, "qualified_dates", dates);
        for (const auto& d : dates) ProductivityRewardMark("qualified", d);
        ParseDateArrayNear(body, "used_dates", dates);
        for (const auto& d : dates) ProductivityRewardMark("used", d);
        int granted = 0;
        if (JsonIntNear(body, "granted", &granted) && granted > 0)
          ProductivityRewardSetGranted(ProductivityRewardGranted() + granted);
      } while (FindNextFileW(h, &fd));
      FindClose(h);
    }
  }

  if (!havePasses) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((bibleDir + L"\\day_passes_*.json").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string body = ReadFileUtf8(bibleDir + L"\\" + fd.cFileName);
        if (body.empty()) continue;
        std::string week;
        JsonStringNear(body, "week", &week);
        std::vector<std::string> dates;
        ParseDateArrayNear(body, "dates", dates);
        for (const auto& d : dates)
          ProductivityInsertPass(d, week.empty() ? d : week);
      } while (FindNextFileW(h, &fd));
      FindClose(h);
    }
  }
}

// ---------------------------------------------------------------------------
// Phase 6a — planner_* CRUD (Focus gateway)
// ---------------------------------------------------------------------------

namespace {

std::string NormalizeTs(const std::string& iso) {
  std::string o = iso;
  for (char& c : o) {
    if (c == 'T' || c == 't') c = ' ';
  }
  size_t cut = o.find('Z');
  if (cut == std::string::npos) cut = o.find('z');
  if (cut == std::string::npos) {
    for (size_t i = 10; i < o.size(); ++i) {
      if (o[i] == '+' || (o[i] == '-' && i > 10)) {
        cut = i;
        break;
      }
    }
  }
  if (cut != std::string::npos) o = o.substr(0, cut);
  size_t dot = o.find('.');
  if (dot != std::string::npos) o = o.substr(0, dot);
  while (!o.empty() && (o.back() == ' ' || o.back() == '\t')) o.pop_back();
  return o;
}

/** Convert any ISO input to UTC `YYYY-MM-DDTHH:MM:SS` for SQLite string compare vs Z stamps. */
std::string ToStoreTs(const std::string& iso) {
  if (iso.empty()) return {};

  // Parse YYYY-MM-DD[T ]HH:MM:SS and optional Z / ±HH:MM
  if (iso.size() < 19 || iso[4] != '-' || iso[7] != '-') {
    std::string n = NormalizeTs(iso);
    if (n.size() >= 19 && n[10] == ' ') {
      return n.substr(0, 10) + "T" + n.substr(11, 8);
    }
    return n;
  }

  int year = atoi(iso.c_str());
  int month = atoi(iso.c_str() + 5);
  int day = atoi(iso.c_str() + 8);
  size_t tpos = iso.find('T');
  if (tpos == std::string::npos) tpos = iso.find('t');
  if (tpos == std::string::npos) tpos = iso.find(' ');
  if (tpos == std::string::npos || tpos + 8 >= iso.size()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT00:00:00", year, month, day);
    return buf;
  }
  int hour = atoi(iso.c_str() + tpos + 1);
  int minute = atoi(iso.c_str() + tpos + 4);
  int second = atoi(iso.c_str() + tpos + 7);

  int offMin = 0;
  bool hasOffset = false;
  size_t z = iso.find('Z');
  if (z == std::string::npos) z = iso.find('z');
  if (z == std::string::npos) {
    for (size_t i = tpos + 8; i < iso.size(); ++i) {
      if (iso[i] == '+' || iso[i] == '-') {
        hasOffset = true;
        int sign = iso[i] == '-' ? -1 : 1;
        int hh = atoi(iso.c_str() + i + 1);
        int mm = 0;
        size_t colon = iso.find(':', i + 1);
        // avoid HH:MM:SS colon — offset colon is after ±HH
        if (colon != std::string::npos && colon > i + 1) mm = atoi(iso.c_str() + colon + 1);
        offMin = sign * (hh * 60 + mm);
        break;
      }
    }
  }

  if (hasOffset && offMin != 0) {
    // wall = UTC + offset → UTC = wall - offset
    long long secs =
        (long long)second + (long long)minute * 60 + (long long)hour * 3600 - (long long)offMin * 60;
    int d = day, mo = month, y = year;
    auto dim = [](int y_, int m_) {
      static const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      int n = mdays[m_];
      if (m_ == 2 && ((y_ % 4 == 0 && y_ % 100 != 0) || (y_ % 400 == 0))) n = 29;
      return n;
    };
    while (secs < 0) {
      secs += 86400;
      d -= 1;
      if (d < 1) {
        mo -= 1;
        if (mo < 1) {
          mo = 12;
          y -= 1;
        }
        d = dim(y, mo);
      }
    }
    while (secs >= 86400) {
      secs -= 86400;
      d += 1;
      if (d > dim(y, mo)) {
        d = 1;
        mo += 1;
        if (mo > 12) {
          mo = 1;
          y += 1;
        }
      }
    }
    hour = (int)(secs / 3600);
    minute = (int)((secs % 3600) / 60);
    second = (int)(secs % 60);
    day = d;
    month = mo;
    year = y;
  }

  char out[32];
  snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour, minute,
           second);
  return out;
}

std::string ToApiIso(const std::string& stored) {
  if (stored.empty()) return "";
  std::string s = stored;
  for (char& c : s) {
    if (c == ' ') c = 'T';
  }
  size_t dot = s.find('.');
  if (dot != std::string::npos) s = s.substr(0, dot);
  size_t z = s.find('Z');
  if (z != std::string::npos) s = s.substr(0, z);
  if (s.size() >= 19) s = s.substr(0, 19);
  if (!s.empty() && s.back() != 'Z') s += "Z";
  return s;
}

std::string MinutesLabel(int totalMinutes) {
  if (totalMinutes < 0) totalMinutes = 0;
  int hours = totalMinutes / 60;
  int mins = totalMinutes % 60;
  char buf[64];
  if (hours == 0) {
    snprintf(buf, sizeof(buf), "0 hours %d min%s", mins, mins == 1 ? "" : "s");
  } else if (mins == 0) {
    snprintf(buf, sizeof(buf), "%d hour%s", hours, hours == 1 ? "" : "s");
  } else {
    snprintf(buf, sizeof(buf), "%d hour%s %d min%s", hours, hours == 1 ? "" : "s", mins,
             mins == 1 ? "" : "s");
  }
  return buf;
}

bool ParseIsoFileTime(const std::string& iso, FILETIME* out) {
  std::string n = NormalizeTs(iso);
  // Accept HH:MM:SS or culture-mangled HH.MM.SS after the date.
  for (size_t i = 11; i < n.size() && i < 19; ++i) {
    if (n[i] == '.') n[i] = ':';
  }
  if (n.size() < 16) return false;
  SYSTEMTIME st{};
  st.wYear = (WORD)atoi(n.c_str());
  st.wMonth = (WORD)atoi(n.c_str() + 5);
  st.wDay = (WORD)atoi(n.c_str() + 8);
  size_t sp = n.find(' ');
  if (sp == std::string::npos || sp + 5 >= n.size()) return false;
  st.wHour = (WORD)atoi(n.c_str() + sp + 1);
  st.wMinute = (WORD)atoi(n.c_str() + sp + 4);
  if (sp + 7 < n.size() && n[sp + 6] == ':') st.wSecond = (WORD)atoi(n.c_str() + sp + 7);
  return SystemTimeToFileTime(&st, out) != 0;
}

int MinutesBetweenIso(const std::string& start, const std::string& end) {
  FILETIME a{}, b{};
  if (!ParseIsoFileTime(start, &a) || !ParseIsoFileTime(end, &b)) return 0;
  ULARGE_INTEGER ua{}, ub{};
  ua.LowPart = a.dwLowDateTime;
  ua.HighPart = a.dwHighDateTime;
  ub.LowPart = b.dwLowDateTime;
  ub.HighPart = b.dwHighDateTime;
  if (ub.QuadPart <= ua.QuadPart) return 1;
  long long secs = (long long)((ub.QuadPart - ua.QuadPart) / 10000000ULL);
  int mins = (int)(secs / 60);
  return mins < 1 ? 1 : mins;
}

bool PayloadGetString(const std::string& body, const char* key, std::string* out) {
  return JsonStringNear(body, key, out);
}

bool PayloadGetInt(const std::string& body, const char* key, int* out) {
  return JsonIntNear(body, key, out);
}

bool PayloadGetBool(const std::string& body, const char* key, bool* out) {
  return JsonBoolNear(body, key, out);
}

bool PayloadGetRaw(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && isspace((unsigned char)body[i])) ++i;
  if (i >= body.size()) return false;
  size_t end = i;
  if (body[i] == '{' || body[i] == '[') {
    char open = body[i];
    char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool inStr = false;
    for (end = i; end < body.size(); ++end) {
      char c = body[end];
      if (inStr) {
        if (c == '\\' && end + 1 < body.size()) {
          ++end;
          continue;
        }
        if (c == '"') inStr = false;
        continue;
      }
      if (c == '"') {
        inStr = true;
        continue;
      }
      if (c == open)
        ++depth;
      else if (c == close) {
        --depth;
        if (depth == 0) {
          ++end;
          break;
        }
      }
    }
  } else if (body[i] == '"') {
    end = i + 1;
    while (end < body.size() && body[end] != '"') {
      if (body[end] == '\\' && end + 1 < body.size())
        end += 2;
      else
        ++end;
    }
    if (end < body.size()) ++end;
  } else if (body.compare(i, 4, "null") == 0) {
    *out = "null";
    return true;
  } else {
    while (end < body.size() && body[end] != ',' && body[end] != '}' && body[end] != ']') ++end;
  }
  *out = body.substr(i, end - i);
  return true;
}

bool PayloadHasKey(const std::string& body, const char* key) {
  std::string needle = std::string("\"") + key + "\"";
  return body.find(needle) != std::string::npos;
}

std::string NullOrQuoted(const std::string& v, bool present) {
  if (!present || v.empty()) return "null";
  return std::string("\"") + JsonEscape(v) + "\"";
}

std::string NullOrInt(long long v, bool present) {
  if (!present) return "null";
  return std::to_string(v);
}

std::string BlockRowJson(sqlite3_stmt* st) {
  auto col = [&](int i) -> std::string {
    const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
    return p ? p : "";
  };
  long long id = sqlite3_column_int64(st, 0);
  std::string title = col(1);
  std::string category = col(2);
  std::string startAt = ToApiIso(col(3));
  std::string endAt = ToApiIso(col(4));
  int planned = sqlite3_column_int(st, 5);
  int remaining = sqlite3_column_int(st, 6);
  std::string status = col(7);
  bool hasRolled = sqlite3_column_type(st, 8) != SQLITE_NULL;
  long long rolled = hasRolled ? sqlite3_column_int64(st, 8) : 0;
  int rollCount = sqlite3_column_int(st, 9);
  bool hasTask = sqlite3_column_type(st, 10) != SQLITE_NULL;
  long long taskId = hasTask ? sqlite3_column_int64(st, 10) : 0;
  bool hasColor = sqlite3_column_type(st, 11) != SQLITE_NULL;
  std::string color = hasColor ? col(11) : "";
  bool hasCreated = sqlite3_column_type(st, 12) != SQLITE_NULL;
  std::string created = hasCreated ? ToApiIso(col(12)) : "";

  return std::string("{\"id\":") + std::to_string(id) + ",\"title\":\"" + JsonEscape(title) +
         "\",\"category\":\"" + JsonEscape(category.empty() ? "study" : category) +
         "\",\"start_at\":\"" + JsonEscape(startAt) + "\",\"end_at\":\"" + JsonEscape(endAt) +
         "\",\"planned_minutes\":" + std::to_string(planned) + ",\"planned_label\":\"" +
         JsonEscape(MinutesLabel(planned)) + "\",\"remaining_minutes\":" +
         std::to_string(remaining) + ",\"remaining_label\":\"" + JsonEscape(MinutesLabel(remaining)) +
         "\",\"status\":\"" + JsonEscape(status.empty() ? "scheduled" : status) +
         "\",\"rolled_from_id\":" + NullOrInt(rolled, hasRolled) +
         ",\"roll_count\":" + std::to_string(rollCount) + ",\"task_id\":" +
         NullOrInt(taskId, hasTask) + ",\"color\":" + NullOrQuoted(color, hasColor) +
         ",\"created_at\":" + NullOrQuoted(created, hasCreated) + "}";
}

const char* kBlockSelectCols =
    "SELECT id, title, category, start_at, end_at, planned_minutes, remaining_minutes, "
    "status, rolled_from_id, roll_count, task_id, color, created_at FROM planner_blocks ";

std::string RoutineRowJson(sqlite3_stmt* st) {
  auto col = [&](int i) -> std::string {
    const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
    return p ? p : "";
  };
  long long id = sqlite3_column_int64(st, 0);
  std::string title = col(1);
  std::string category = col(2);
  std::string startTime = col(3);
  bool hasEnd = sqlite3_column_type(st, 4) != SQLITE_NULL;
  std::string endTime = hasEnd ? col(4) : "";
  bool hasDur = sqlite3_column_type(st, 5) != SQLITE_NULL;
  int dur = hasDur ? sqlite3_column_int(st, 5) : 0;
  std::string daysJson = col(6);
  if (daysJson.empty()) daysJson = "[\"mon\",\"tue\",\"wed\",\"thu\",\"fri\",\"sat\",\"sun\"]";
  bool hasColor = sqlite3_column_type(st, 7) != SQLITE_NULL;
  std::string color = hasColor ? col(7) : "";
  bool enabled = sqlite3_column_int(st, 8) != 0;
  int sortOrder = sqlite3_column_int(st, 9);

  return std::string("{\"id\":") + std::to_string(id) + ",\"title\":\"" + JsonEscape(title) +
         "\",\"category\":\"" + JsonEscape(category.empty() ? "personal" : category) +
         "\",\"start_time\":\"" + JsonEscape(startTime) + "\",\"end_time\":" +
         NullOrQuoted(endTime, hasEnd) + ",\"duration_minutes\":" +
         (hasDur ? std::to_string(dur) : "null") + ",\"days\":" + daysJson + ",\"color\":" +
         NullOrQuoted(color, hasColor) + ",\"enabled\":" + (enabled ? "true" : "false") +
         ",\"sort_order\":" + std::to_string(sortOrder) + "}";
}

const char* kRoutineSelectCols =
    "SELECT id, title, category, start_time, end_time, duration_minutes, days_json, color, "
    "enabled, sort_order FROM planner_routines ";

}  // namespace

void ProductivityEnsurePlanner() {
  if (!gDb) return;
  Exec(
      "CREATE TABLE IF NOT EXISTS planner_blocks ("
      "  id INTEGER PRIMARY KEY,"
      "  user_id INTEGER NOT NULL,"
      "  title VARCHAR NOT NULL,"
      "  category VARCHAR NOT NULL DEFAULT 'study',"
      "  start_at DATETIME NOT NULL,"
      "  end_at DATETIME NOT NULL,"
      "  planned_minutes INTEGER NOT NULL,"
      "  remaining_minutes INTEGER NOT NULL,"
      "  status VARCHAR NOT NULL DEFAULT 'scheduled',"
      "  rolled_from_id INTEGER,"
      "  roll_count INTEGER NOT NULL DEFAULT 0,"
      "  task_id INTEGER,"
      "  color VARCHAR,"
      "  created_at DATETIME"
      ");"
      "CREATE TABLE IF NOT EXISTS planner_routines ("
      "  id INTEGER PRIMARY KEY,"
      "  user_id INTEGER NOT NULL,"
      "  title VARCHAR NOT NULL,"
      "  category VARCHAR NOT NULL DEFAULT 'personal',"
      "  start_time VARCHAR NOT NULL,"
      "  end_time VARCHAR,"
      "  duration_minutes INTEGER,"
      "  days_json TEXT NOT NULL DEFAULT '[\"mon\",\"tue\",\"wed\",\"thu\",\"fri\",\"sat\",\"sun\"]',"
      "  color VARCHAR,"
      "  enabled BOOLEAN NOT NULL DEFAULT 1,"
      "  sort_order INTEGER NOT NULL DEFAULT 0,"
      "  created_at DATETIME"
      ");"
      "CREATE TABLE IF NOT EXISTS productivity_planner_meta ("
      "  id INTEGER PRIMARY KEY CHECK (id = 1),"
      "  imported_at TEXT,"
      "  block_count INTEGER NOT NULL DEFAULT 0,"
      "  routine_count INTEGER NOT NULL DEFAULT 0"
      ");");
  Exec("INSERT OR IGNORE INTO productivity_planner_meta (id, imported_at, block_count, "
       "routine_count) VALUES (1, NULL, 0, 0);");

  // One-time claim: existing Python planner_* rows become enforcer SoT (same tables).
  sqlite3_stmt* st = nullptr;
  bool already = false;
  if (sqlite3_prepare_v2(gDb, "SELECT imported_at FROM productivity_planner_meta WHERE id=1;", -1,
                         &st, nullptr) == SQLITE_OK) {
    if (sqlite3_step(st) == SQLITE_ROW) {
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
      already = p && *p;
    }
    sqlite3_finalize(st);
  }
  if (already) return;

  int blocks = ScalarInt("SELECT COUNT(*) FROM planner_blocks;", nullptr, nullptr);
  int routines = ScalarInt("SELECT COUNT(*) FROM planner_routines;", nullptr, nullptr);
  std::string now = IsoLocalNow();
  if (sqlite3_prepare_v2(gDb,
                         "UPDATE productivity_planner_meta SET imported_at=?, block_count=?, "
                         "routine_count=? WHERE id=1;",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, blocks);
    sqlite3_bind_int(st, 3, routines);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

std::string ProductivityPlanListJson(const std::string& fromIso, const std::string& toIso,
                                     int userId) {
  if (!gDb) return "[]";
  ProductivityEnsurePlanner();
  std::string from = ToStoreTs(fromIso);
  std::string to = ToStoreTs(toIso);
  sqlite3_stmt* st = nullptr;
  std::string sql = std::string(kBlockSelectCols) +
                    "WHERE user_id=? AND start_at < ? AND end_at > ? ORDER BY start_at ASC;";
  if (sqlite3_prepare_v2(gDb, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return "[]";
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, to.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, from.c_str(), -1, SQLITE_TRANSIENT);
  std::string out = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    if (!first) out += ",";
    first = false;
    out += BlockRowJson(st);
  }
  sqlite3_finalize(st);
  out += "]";
  return out;
}

std::string ProductivityPlanGetJson(long long id, int userId) {
  if (!gDb || id <= 0) return "";
  ProductivityEnsurePlanner();
  sqlite3_stmt* st = nullptr;
  std::string sql = std::string(kBlockSelectCols) + "WHERE id=? AND user_id=?;";
  if (sqlite3_prepare_v2(gDb, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return "";
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  std::string out;
  if (sqlite3_step(st) == SQLITE_ROW) out = BlockRowJson(st);
  sqlite3_finalize(st);
  return out;
}

bool ProductivityPlanUpsert(const std::string& payloadJson, int userId, std::string* outBlockJson) {
  if (!gDb) return false;
  ProductivityEnsurePlanner();

  int id = 0;
  PayloadGetInt(payloadJson, "id", &id);

  if (id > 0) {
    // Load existing then patch.
    std::string existing = ProductivityPlanGetJson(id, userId);
    if (existing.empty()) return false;

    std::string title, category, startAt, endAt, status, color;
    int duration = 0, remaining = -1;
    bool hasTitle = PayloadGetString(payloadJson, "title", &title);
    bool hasCat = PayloadGetString(payloadJson, "category", &category);
    bool hasStart = PayloadGetString(payloadJson, "start_at", &startAt);
    bool hasEnd = PayloadGetString(payloadJson, "end_at", &endAt);
    bool hasDur = PayloadGetInt(payloadJson, "duration_minutes", &duration);
    bool hasRem = PayloadGetInt(payloadJson, "remaining_minutes", &remaining);
    bool hasStatus = PayloadGetString(payloadJson, "status", &status);
    bool hasColor = PayloadHasKey(payloadJson, "color");
    if (hasColor) {
      std::string raw;
      if (PayloadGetRaw(payloadJson, "color", &raw)) {
        if (raw == "null")
          color.clear();
        else
          PayloadGetString(payloadJson, "color", &color);
      }
    }

    // Fetch current row fields for merge when only duration/start change.
    sqlite3_stmt* st = nullptr;
    std::string curStart, curEnd, curTitle, curCat, curStatus, curColor;
    int curPlanned = 0, curRemaining = 0;
    bool curHasColor = false;
    if (sqlite3_prepare_v2(gDb,
                           "SELECT title, category, start_at, end_at, planned_minutes, "
                           "remaining_minutes, status, color FROM planner_blocks WHERE id=? AND "
                           "user_id=?;",
                           -1, &st, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int(st, 2, userId);
    if (sqlite3_step(st) != SQLITE_ROW) {
      sqlite3_finalize(st);
      return false;
    }
    auto col = [&](int i) -> std::string {
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
      return p ? p : "";
    };
    curTitle = col(0);
    curCat = col(1);
    curStart = col(2);
    curEnd = col(3);
    curPlanned = sqlite3_column_int(st, 4);
    curRemaining = sqlite3_column_int(st, 5);
    curStatus = col(6);
    curHasColor = sqlite3_column_type(st, 7) != SQLITE_NULL;
    curColor = curHasColor ? col(7) : "";
    sqlite3_finalize(st);

    if (!hasTitle) title = curTitle;
    if (!hasCat) category = curCat;
    if (!hasStart) startAt = curStart;
    else startAt = ToStoreTs(startAt);
    if (!hasStatus) status = curStatus;
    if (!hasColor) {
      color = curColor;
      hasColor = curHasColor;
    }

    int planned = curPlanned;
    if (hasDur && duration > 0) {
      planned = duration;
      endAt = "";  // recompute
      hasEnd = false;
    }
    if (hasEnd) {
      endAt = ToStoreTs(endAt);
      planned = MinutesBetweenIso(startAt, endAt);
    } else if (hasDur && duration > 0) {
      // add minutes to start
      FILETIME ft{};
      if (!ParseIsoFileTime(startAt, &ft)) return false;
      ULARGE_INTEGER uli;
      uli.LowPart = ft.dwLowDateTime;
      uli.HighPart = ft.dwHighDateTime;
      uli.QuadPart += (ULONGLONG)duration * 60ULL * 10000000ULL;
      ft.dwLowDateTime = uli.LowPart;
      ft.dwHighDateTime = uli.HighPart;
      SYSTEMTIME outSt;
      FileTimeToSystemTime(&ft, &outSt);
      char buf[40];
      snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", outSt.wYear, outSt.wMonth,
               outSt.wDay, outSt.wHour, outSt.wMinute, outSt.wSecond);
      endAt = buf;
    } else {
      endAt = ToStoreTs(curEnd);
    }
    startAt = ToStoreTs(startAt);

    int rem = hasRem ? remaining : curRemaining;
    if (hasDur && duration > 0 && !hasRem) rem = planned;

    if (sqlite3_prepare_v2(gDb,
                           "UPDATE planner_blocks SET title=?, category=?, start_at=?, end_at=?, "
                           "planned_minutes=?, remaining_minutes=?, status=?, color=? WHERE id=? AND "
                           "user_id=?;",
                           -1, &st, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_text(st, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, startAt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, endAt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, planned);
    sqlite3_bind_int(st, 6, rem);
    sqlite3_bind_text(st, 7, status.c_str(), -1, SQLITE_TRANSIENT);
    if (hasColor && !color.empty())
      sqlite3_bind_text(st, 8, color.c_str(), -1, SQLITE_TRANSIENT);
    else if (hasColor)
      sqlite3_bind_null(st, 8);
    else if (curHasColor)
      sqlite3_bind_text(st, 8, curColor.c_str(), -1, SQLITE_TRANSIENT);
    else
      sqlite3_bind_null(st, 8);
    sqlite3_bind_int64(st, 9, id);
    sqlite3_bind_int(st, 10, userId);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
    ProductivityBumpSeq();
    if (outBlockJson) *outBlockJson = ProductivityPlanGetJson(id, userId);
    return true;
  }

  // Create
  std::string title, category = "study", startAt, endAt, color;
  int duration = 0;
  if (!PayloadGetString(payloadJson, "title", &title) || title.empty()) return false;
  PayloadGetString(payloadJson, "category", &category);
  if (!PayloadGetString(payloadJson, "start_at", &startAt) || startAt.empty()) return false;
  bool hasEnd = PayloadGetString(payloadJson, "end_at", &endAt);
  bool hasDur = PayloadGetInt(payloadJson, "duration_minutes", &duration);
  PayloadGetString(payloadJson, "color", &color);
  startAt = ToStoreTs(startAt);

  int planned = 0;
  if (hasDur && duration > 0) {
    planned = duration;
    FILETIME ft{};
    if (!ParseIsoFileTime(startAt, &ft)) return false;
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uli.QuadPart += (ULONGLONG)duration * 60ULL * 10000000ULL;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME outSt;
    FileTimeToSystemTime(&ft, &outSt);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", outSt.wYear, outSt.wMonth, outSt.wDay,
             outSt.wHour, outSt.wMinute, outSt.wSecond);
    endAt = buf;
  } else if (hasEnd && !endAt.empty()) {
    endAt = ToStoreTs(endAt);
    planned = MinutesBetweenIso(startAt, endAt);
  } else {
    return false;
  }

  std::string now = IsoLocalNow();
  std::string created = ToStoreTs(now);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT INTO planner_blocks (user_id, title, category, start_at, end_at, "
                         "planned_minutes, remaining_minutes, status, roll_count, color, "
                         "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, 'scheduled', 0, ?, ?);",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, startAt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, endAt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 6, planned);
  sqlite3_bind_int(st, 7, planned);
  if (!color.empty())
    sqlite3_bind_text(st, 8, color.c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(st, 8);
  sqlite3_bind_text(st, 9, created.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  if (!ok) return false;
  long long newId = sqlite3_last_insert_rowid(gDb);
  ProductivityBumpSeq();
  if (outBlockJson) *outBlockJson = ProductivityPlanGetJson(newId, userId);
  return true;
}

bool ProductivityPlanDelete(long long id, int userId) {
  if (!gDb || id <= 0) return false;
  ProductivityEnsurePlanner();
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb, "DELETE FROM planner_blocks WHERE id=? AND user_id=?;", -1, &st,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  int changes = sqlite3_changes(gDb);
  sqlite3_finalize(st);
  if (!ok || changes <= 0) return false;
  ProductivityBumpSeq();
  return true;
}

std::string ProductivityRoutineListJson(int userId) {
  if (!gDb) return "[]";
  ProductivityEnsurePlanner();
  sqlite3_stmt* st = nullptr;
  std::string sql =
      std::string(kRoutineSelectCols) + "WHERE user_id=? ORDER BY sort_order ASC, id ASC;";
  if (sqlite3_prepare_v2(gDb, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return "[]";
  sqlite3_bind_int(st, 1, userId);
  std::string out = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    if (!first) out += ",";
    first = false;
    out += RoutineRowJson(st);
  }
  sqlite3_finalize(st);
  out += "]";
  return out;
}

bool ProductivityRoutineUpsert(const std::string& payloadJson, int userId,
                               std::string* outRoutineJson) {
  if (!gDb) return false;
  ProductivityEnsurePlanner();
  int id = 0;
  PayloadGetInt(payloadJson, "id", &id);

  auto fetchOne = [&](long long rid) -> std::string {
    sqlite3_stmt* st = nullptr;
    std::string sql = std::string(kRoutineSelectCols) + "WHERE id=? AND user_id=?;";
    if (sqlite3_prepare_v2(gDb, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_int64(st, 1, rid);
    sqlite3_bind_int(st, 2, userId);
    std::string out;
    if (sqlite3_step(st) == SQLITE_ROW) out = RoutineRowJson(st);
    sqlite3_finalize(st);
    return out;
  };

  if (id > 0) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(gDb,
                           "SELECT title, category, start_time, end_time, duration_minutes, "
                           "days_json, color, enabled, sort_order FROM planner_routines WHERE "
                           "id=? AND user_id=?;",
                           -1, &st, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int(st, 2, userId);
    if (sqlite3_step(st) != SQLITE_ROW) {
      sqlite3_finalize(st);
      return false;
    }
    auto col = [&](int i) -> std::string {
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
      return p ? p : "";
    };
    std::string title = col(0), category = col(1), startTime = col(2), endTime = col(3);
    bool hasEnd = sqlite3_column_type(st, 3) != SQLITE_NULL;
    bool hasDur = sqlite3_column_type(st, 4) != SQLITE_NULL;
    int dur = hasDur ? sqlite3_column_int(st, 4) : 0;
    std::string daysJson = col(5);
    bool hasColor = sqlite3_column_type(st, 6) != SQLITE_NULL;
    std::string color = hasColor ? col(6) : "";
    bool enabled = sqlite3_column_int(st, 7) != 0;
    int sortOrder = sqlite3_column_int(st, 8);
    sqlite3_finalize(st);

    std::string t;
    if (PayloadGetString(payloadJson, "title", &t) && !t.empty()) title = t;
    if (PayloadGetString(payloadJson, "category", &t) && !t.empty()) category = t;
    if (PayloadGetString(payloadJson, "start_time", &t) && !t.empty()) startTime = t;
    if (PayloadHasKey(payloadJson, "end_time")) {
      std::string raw;
      PayloadGetRaw(payloadJson, "end_time", &raw);
      if (raw == "null") {
        endTime.clear();
        hasEnd = false;
      } else if (PayloadGetString(payloadJson, "end_time", &t)) {
        endTime = t;
        hasEnd = !t.empty();
      }
    }
    if (PayloadHasKey(payloadJson, "duration_minutes")) {
      std::string raw;
      PayloadGetRaw(payloadJson, "duration_minutes", &raw);
      if (raw == "null") {
        hasDur = false;
        dur = 0;
      } else if (PayloadGetInt(payloadJson, "duration_minutes", &dur)) {
        hasDur = true;
      }
    }
    if (PayloadHasKey(payloadJson, "days")) {
      std::string raw;
      if (PayloadGetRaw(payloadJson, "days", &raw) && !raw.empty() && raw[0] == '[') daysJson = raw;
    }
    if (PayloadHasKey(payloadJson, "color")) {
      std::string raw;
      PayloadGetRaw(payloadJson, "color", &raw);
      if (raw == "null") {
        color.clear();
        hasColor = false;
      } else if (PayloadGetString(payloadJson, "color", &t)) {
        color = t;
        hasColor = !t.empty();
      }
    }
    bool en = enabled;
    if (PayloadGetBool(payloadJson, "enabled", &en)) enabled = en;
    int so = sortOrder;
    if (PayloadGetInt(payloadJson, "sort_order", &so)) sortOrder = so;

    if (sqlite3_prepare_v2(gDb,
                           "UPDATE planner_routines SET title=?, category=?, start_time=?, end_time=?, "
                           "duration_minutes=?, days_json=?, color=?, enabled=?, sort_order=? WHERE "
                           "id=? AND user_id=?;",
                           -1, &st, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_text(st, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, startTime.c_str(), -1, SQLITE_TRANSIENT);
    if (hasEnd)
      sqlite3_bind_text(st, 4, endTime.c_str(), -1, SQLITE_TRANSIENT);
    else
      sqlite3_bind_null(st, 4);
    if (hasDur)
      sqlite3_bind_int(st, 5, dur);
    else
      sqlite3_bind_null(st, 5);
    sqlite3_bind_text(st, 6, daysJson.c_str(), -1, SQLITE_TRANSIENT);
    if (hasColor)
      sqlite3_bind_text(st, 7, color.c_str(), -1, SQLITE_TRANSIENT);
    else
      sqlite3_bind_null(st, 7);
    sqlite3_bind_int(st, 8, enabled ? 1 : 0);
    sqlite3_bind_int(st, 9, sortOrder);
    sqlite3_bind_int64(st, 10, id);
    sqlite3_bind_int(st, 11, userId);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
    ProductivityBumpSeq();
    if (outRoutineJson) *outRoutineJson = fetchOne(id);
    return true;
  }

  std::string title, category = "personal", startTime, endTime, color;
  int duration = 0, sortOrder = 0;
  bool enabled = true;
  if (!PayloadGetString(payloadJson, "title", &title) || title.empty()) return false;
  PayloadGetString(payloadJson, "category", &category);
  if (!PayloadGetString(payloadJson, "start_time", &startTime) || startTime.empty()) return false;
  bool hasEnd = PayloadGetString(payloadJson, "end_time", &endTime);
  bool hasDur = PayloadGetInt(payloadJson, "duration_minutes", &duration);
  PayloadGetString(payloadJson, "color", &color);
  PayloadGetBool(payloadJson, "enabled", &enabled);
  PayloadGetInt(payloadJson, "sort_order", &sortOrder);
  std::string daysJson = "[\"mon\",\"tue\",\"wed\",\"thu\",\"fri\",\"sat\",\"sun\"]";
  std::string rawDays;
  if (PayloadGetRaw(payloadJson, "days", &rawDays) && !rawDays.empty() && rawDays[0] == '[')
    daysJson = rawDays;

  std::string created = ToStoreTs(IsoLocalNow());
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT INTO planner_routines (user_id, title, category, start_time, "
                         "end_time, duration_minutes, days_json, color, enabled, sort_order, "
                         "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, startTime.c_str(), -1, SQLITE_TRANSIENT);
  if (hasEnd && !endTime.empty())
    sqlite3_bind_text(st, 5, endTime.c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(st, 5);
  if (hasDur && duration > 0)
    sqlite3_bind_int(st, 6, duration);
  else
    sqlite3_bind_null(st, 6);
  sqlite3_bind_text(st, 7, daysJson.c_str(), -1, SQLITE_TRANSIENT);
  if (!color.empty())
    sqlite3_bind_text(st, 8, color.c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(st, 8);
  sqlite3_bind_int(st, 9, enabled ? 1 : 0);
  sqlite3_bind_int(st, 10, sortOrder);
  sqlite3_bind_text(st, 11, created.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  if (!ok) return false;
  long long newId = sqlite3_last_insert_rowid(gDb);
  ProductivityBumpSeq();
  if (outRoutineJson) *outRoutineJson = fetchOne(newId);
  return true;
}

bool ProductivityRoutineDelete(long long id, int userId) {
  if (!gDb || id <= 0) return false;
  ProductivityEnsurePlanner();
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb, "DELETE FROM planner_routines WHERE id=? AND user_id=?;", -1, &st,
                         nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  int changes = sqlite3_changes(gDb);
  sqlite3_finalize(st);
  if (!ok || changes <= 0) return false;
  ProductivityBumpSeq();
  return true;
}

namespace {

std::string AddMinutesToIso(const std::string& startIso, int minutes) {
  FILETIME ft{};
  if (!ParseIsoFileTime(startIso, &ft)) return startIso;
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  uli.QuadPart += (ULONGLONG)(minutes > 0 ? minutes : 0) * 60ULL * 10000000ULL;
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  SYSTEMTIME outSt;
  FileTimeToSystemTime(&ft, &outSt);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", outSt.wYear, outSt.wMonth, outSt.wDay,
           outSt.wHour, outSt.wMinute, outSt.wSecond);
  return buf;
}

bool BlockOverlaps(int userId, const std::string& startAt, const std::string& endAt,
                   long long excludeId) {
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT 1 FROM planner_blocks WHERE user_id=? AND status IN ('scheduled','in_progress') "
      "AND start_at < ? AND end_at > ? AND id != ? LIMIT 1;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, endAt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, startAt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 4, excludeId);
  bool hit = sqlite3_step(st) == SQLITE_ROW;
  sqlite3_finalize(st);
  return hit;
}

std::string WeekdayKeyLocal(const SYSTEMTIME& st) {
  static const char* keys[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
  int d = st.wDayOfWeek;
  if (d < 0 || d > 6) d = 0;
  return keys[d];
}

bool ParseHm(const std::string& hm, int* hour, int* minute) {
  if (hm.size() < 4) return false;
  int h = 0, m = 0;
  if (sscanf(hm.c_str(), "%d:%d", &h, &m) < 2) return false;
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  *hour = h;
  *minute = m;
  return true;
}

bool DayInRoutineDays(const std::string& daysJson, const std::string& dayKey) {
  std::string lower = daysJson;
  for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (lower.find("daily") != std::string::npos) return true;
  return lower.find(dayKey) != std::string::npos;
}

}  // namespace

bool ProductivityPlanStart(long long id, int userId, std::string* outBlockJson) {
  if (!gDb || id <= 0) return false;
  ProductivityEnsurePlanner();
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "UPDATE planner_blocks SET status='in_progress' WHERE id=? AND user_id=? "
                         "AND status NOT IN ('done','rolled','cancelled');",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  if (!ok || sqlite3_changes(gDb) <= 0) {
    std::string cur = ProductivityPlanGetJson(id, userId);
    if (cur.empty()) return false;
    if (outBlockJson) *outBlockJson = cur;
    return true;
  }
  ProductivityBumpSeq();
  if (outBlockJson) *outBlockJson = ProductivityPlanGetJson(id, userId);
  return true;
}

bool ProductivityPlanComplete(long long id, int userId, int minutesSpentOrNeg1,
                              std::string* outBlockJson) {
  if (!gDb || id <= 0) return false;
  ProductivityEnsurePlanner();
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT remaining_minutes, status, start_at FROM planner_blocks WHERE "
                         "id=? AND user_id=?;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  if (sqlite3_step(st) != SQLITE_ROW) {
    sqlite3_finalize(st);
    return false;
  }
  int remaining = sqlite3_column_int(st, 0);
  const char* statusP = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
  std::string status = statusP ? statusP : "";
  const char* startP = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
  std::string startAt = startP ? startP : "";
  sqlite3_finalize(st);

  if (status == "done" || status == "rolled" || status == "cancelled") {
    if (outBlockJson) *outBlockJson = ProductivityPlanGetJson(id, userId);
    return true;
  }

  if (minutesSpentOrNeg1 >= 0)
    remaining = remaining - minutesSpentOrNeg1;
  else
    remaining = 0;
  if (remaining < 0) remaining = 0;

  std::string newStatus = remaining <= 0 ? "done" : status;
  std::string newEnd;
  if (remaining <= 0) {
    remaining = 0;
    newStatus = "done";
  } else {
    newEnd = AddMinutesToIso(startAt, remaining);
  }

  if (remaining <= 0) {
    if (sqlite3_prepare_v2(gDb,
                           "UPDATE planner_blocks SET remaining_minutes=?, status=? WHERE id=? AND "
                           "user_id=?;",
                           -1, &st, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_int(st, 1, remaining);
    sqlite3_bind_text(st, 2, newStatus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, id);
    sqlite3_bind_int(st, 4, userId);
  } else {
    if (sqlite3_prepare_v2(gDb,
                           "UPDATE planner_blocks SET remaining_minutes=?, status=?, end_at=? WHERE "
                           "id=? AND user_id=?;",
                           -1, &st, nullptr) != SQLITE_OK) {
      return false;
    }
    sqlite3_bind_int(st, 1, remaining);
    sqlite3_bind_text(st, 2, newStatus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, newEnd.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, id);
    sqlite3_bind_int(st, 5, userId);
  }
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  if (!ok) return false;
  ProductivityBumpSeq();
  if (outBlockJson) *outBlockJson = ProductivityPlanGetJson(id, userId);
  return true;
}

bool ProductivityPlanRollForward(long long id, int userId, const std::string& newStartIsoOrEmpty,
                                 std::string* outRolledJson, std::string* outNewJson) {
  if (!gDb || id <= 0) return false;
  ProductivityEnsurePlanner();

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT title, category, remaining_minutes, planned_minutes, roll_count, "
                         "task_id, color, status FROM planner_blocks WHERE id=? AND user_id=?;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  if (sqlite3_step(st) != SQLITE_ROW) {
    sqlite3_finalize(st);
    return false;
  }
  auto col = [&](int i) -> std::string {
    const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
    return p ? p : "";
  };
  std::string title = col(0);
  std::string category = col(1);
  int remaining = sqlite3_column_int(st, 2);
  int planned = sqlite3_column_int(st, 3);
  int rollCount = sqlite3_column_int(st, 4);
  bool hasTask = sqlite3_column_type(st, 5) != SQLITE_NULL;
  long long taskId = hasTask ? sqlite3_column_int64(st, 5) : 0;
  bool hasColor = sqlite3_column_type(st, 6) != SQLITE_NULL;
  std::string color = hasColor ? col(6) : "";
  sqlite3_finalize(st);

  if (remaining <= 0) {
    return ProductivityPlanComplete(id, userId, -1, outRolledJson);
  }

  if (sqlite3_prepare_v2(gDb, "UPDATE planner_blocks SET status='rolled' WHERE id=? AND user_id=?;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(st, 1, id);
  sqlite3_bind_int(st, 2, userId);
  if (sqlite3_step(st) != SQLITE_DONE) {
    sqlite3_finalize(st);
    return false;
  }
  sqlite3_finalize(st);

  std::string startAt;
  if (!newStartIsoOrEmpty.empty()) {
    startAt = ToStoreTs(newStartIsoOrEmpty);
  } else {
    SYSTEMTIME now;
    GetLocalTime(&now);
    now.wMinute = 0;
    now.wSecond = 0;
    now.wMilliseconds = 0;
    FILETIME ft;
    SystemTimeToFileTime(&now, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uli.QuadPart += 60ULL * 60ULL * 10000000ULL;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME st2;
    FileTimeToSystemTime(&ft, &st2);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", st2.wYear, st2.wMonth, st2.wDay,
             st2.wHour, st2.wMinute, st2.wSecond);
    startAt = buf;
  }
  std::string endAt = AddMinutesToIso(startAt, remaining);
  std::string created = ToStoreTs(IsoLocalNow());

  if (sqlite3_prepare_v2(gDb,
                         "INSERT INTO planner_blocks (user_id, title, category, start_at, end_at, "
                         "planned_minutes, remaining_minutes, status, rolled_from_id, roll_count, "
                         "task_id, color, created_at) VALUES (?,?,?,?,?,?,?,'scheduled',?,?,?,?,?);",
                         -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, category.empty() ? "study" : category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, startAt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, endAt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 6, planned);
  sqlite3_bind_int(st, 7, remaining);
  sqlite3_bind_int64(st, 8, id);
  sqlite3_bind_int(st, 9, rollCount + 1);
  if (hasTask)
    sqlite3_bind_int64(st, 10, taskId);
  else
    sqlite3_bind_null(st, 10);
  if (hasColor)
    sqlite3_bind_text(st, 11, color.c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(st, 11);
  sqlite3_bind_text(st, 12, created.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  if (!ok) return false;
  long long newId = sqlite3_last_insert_rowid(gDb);
  ProductivityBumpSeq();
  if (outRolledJson) *outRolledJson = ProductivityPlanGetJson(id, userId);
  if (outNewJson) *outNewJson = ProductivityPlanGetJson(newId, userId);
  return true;
}

int ProductivityRoutineApply(int userId, const std::string& dateYmdOrEmpty, bool skipOverlaps) {
  if (!gDb) return -1;
  ProductivityEnsurePlanner();

  SYSTEMTIME daySt;
  GetLocalTime(&daySt);
  if (!dateYmdOrEmpty.empty() && dateYmdOrEmpty.size() >= 10) {
    int y = 0, mo = 0, d = 0;
    if (sscanf(dateYmdOrEmpty.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) {
      daySt.wYear = (WORD)y;
      daySt.wMonth = (WORD)mo;
      daySt.wDay = (WORD)d;
      daySt.wHour = 12;
      daySt.wMinute = 0;
      daySt.wSecond = 0;
      FILETIME ft;
      SystemTimeToFileTime(&daySt, &ft);
      FileTimeToSystemTime(&ft, &daySt);
    }
  }
  std::string dayKey = WeekdayKeyLocal(daySt);

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT id, title, category, start_time, end_time, duration_minutes, "
                         "days_json, color FROM planner_routines WHERE user_id=? AND enabled=1 "
                         "ORDER BY sort_order ASC, id ASC;",
                         -1, &st, nullptr) != SQLITE_OK) {
    return -1;
  }
  sqlite3_bind_int(st, 1, userId);

  int created = 0;
  while (sqlite3_step(st) == SQLITE_ROW) {
    auto col = [&](int i) -> std::string {
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
      return p ? p : "";
    };
    std::string title = col(1);
    std::string category = col(2);
    std::string startHm = col(3);
    bool hasEnd = sqlite3_column_type(st, 4) != SQLITE_NULL;
    std::string endHm = hasEnd ? col(4) : "";
    bool hasDur = sqlite3_column_type(st, 5) != SQLITE_NULL;
    int dur = hasDur ? sqlite3_column_int(st, 5) : 60;
    std::string daysJson = col(6);
    bool hasColor = sqlite3_column_type(st, 7) != SQLITE_NULL;
    std::string color = hasColor ? col(7) : "";

    if (!DayInRoutineDays(daysJson, dayKey)) continue;

    int sh = 9, sm = 0;
    if (!ParseHm(startHm, &sh, &sm)) continue;
    char startBuf[40];
    snprintf(startBuf, sizeof(startBuf), "%04u-%02u-%02u %02u:%02u:00", daySt.wYear, daySt.wMonth,
             daySt.wDay, sh, sm);
    std::string startAt = startBuf;
    std::string endAt;
    if (hasEnd && !endHm.empty()) {
      int eh = 10, em = 0;
      if (!ParseHm(endHm, &eh, &em)) continue;
      char endBuf[40];
      snprintf(endBuf, sizeof(endBuf), "%04u-%02u-%02u %02u:%02u:00", daySt.wYear, daySt.wMonth,
               daySt.wDay, eh, em);
      endAt = endBuf;
    } else {
      if (dur <= 0) dur = 60;
      endAt = AddMinutesToIso(startAt, dur);
    }
    int minutes = MinutesBetweenIso(startAt, endAt);
    if (minutes <= 0) continue;
    if (skipOverlaps && BlockOverlaps(userId, startAt, endAt, 0)) continue;

    std::string payload = std::string("{\"title\":\"") + JsonEscape(title) +
                          "\",\"category\":\"" +
                          JsonEscape(category.empty() ? "personal" : category) +
                          "\",\"start_at\":\"" + JsonEscape(ToApiIso(startAt)) +
                          "\",\"end_at\":\"" + JsonEscape(ToApiIso(endAt)) +
                          "\",\"planned_minutes\":" + std::to_string(minutes) +
                          ",\"remaining_minutes\":" + std::to_string(minutes) +
                          ",\"status\":\"scheduled\"";
    if (hasColor && !color.empty())
      payload += ",\"color\":\"" + JsonEscape(color) + "\"";
    payload += "}";
    std::string out;
    if (ProductivityPlanUpsert(payload, userId, &out)) ++created;
  }
  sqlite3_finalize(st);
  return created;
}

std::string ProductivityPlanOverlayJson(const std::string& fromIso, const std::string& toIso,
                                        int userId) {
  if (!gDb) return "{\"sessions\":[],\"hour_slices\":[]}";
  std::string from = ToStoreTs(fromIso);
  std::string to = ToStoreTs(toIso);
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT session_id, start_time, end_time, source, category, app_name, window_title, "
      "task_id FROM tracked_sessions WHERE user_id=? AND start_time < ? AND "
      "(end_time IS NULL OR end_time = '' OR end_time > ?) "
      "ORDER BY start_time ASC LIMIT 500;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) {
    return "{\"sessions\":[],\"hour_slices\":[]}";
  }
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, to.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, from.c_str(), -1, SQLITE_TRANSIENT);
  std::string sessions = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    auto col = [&](int i) -> std::string {
      const char* p = reinterpret_cast<const char*>(sqlite3_column_text(st, i));
      return p ? p : "";
    };
    if (!first) sessions += ",";
    first = false;
    std::string sid = col(0);
    std::string start = ToApiIso(col(1));
    std::string end = ToApiIso(col(2));
    std::string source = col(3);
    std::string category = col(4);
    std::string app = col(5);
    std::string title = col(6);
    if (title.size() > 80) title = title.substr(0, 80);
    bool hasTask = sqlite3_column_type(st, 7) != SQLITE_NULL;
    long long taskId = hasTask ? sqlite3_column_int64(st, 7) : 0;
    sessions += "{\"session_id\":\"" + JsonEscape(sid) + "\",\"start_time\":\"" +
                JsonEscape(start) + "\",\"end_time\":\"" + JsonEscape(end) + "\",\"source\":\"" +
                JsonEscape(source) + "\",\"category\":" +
                (category.empty() ? "null" : ("\"" + JsonEscape(category) + "\"")) +
                ",\"app_name\":" + (app.empty() ? "null" : ("\"" + JsonEscape(app) + "\"")) +
                ",\"window_title\":" +
                (title.empty() ? "null" : ("\"" + JsonEscape(title) + "\"")) + ",\"task_id\":" +
                (hasTask ? std::to_string(taskId) : "null") + "}";
  }
  sqlite3_finalize(st);
  sessions += "]";
  return "{\"sessions\":" + sessions + ",\"hour_slices\":[]}";
}

std::string ProductivityPlanAdherenceJson(const std::string& dayYmd, int userId) {
  if (!gDb) return "{}";
  std::string day = dayYmd;
  if (day.size() < 10) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    day = buf;
  } else {
    day = day.substr(0, 10);
  }
  std::string from = day + " 00:00:00";
  std::string to = day + " 23:59:59";

  int planned = 0;
  int blockCount = 0;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT planned_minutes FROM planner_blocks WHERE user_id=? AND "
                         "start_at <= ? AND end_at >= ? AND status NOT IN ('cancelled');",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(st, 1, userId);
    sqlite3_bind_text(st, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, from.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
      planned += sqlite3_column_int(st, 0);
      ++blockCount;
    }
    sqlite3_finalize(st);
  }

  int actualSec = 0;
  int sessionCount = 0;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT start_time, end_time FROM tracked_sessions WHERE user_id=? AND "
                         "start_time < ? AND end_time > ?;",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(st, 1, userId);
    sqlite3_bind_text(st, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, from.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
      const char* a = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
      const char* b = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
      int mins = MinutesBetweenIso(a ? a : "", b ? b : "");
      if (mins > 0) actualSec += mins * 60;
      ++sessionCount;
    }
    sqlite3_finalize(st);
  }
  int actualMin = actualSec / 60;
  int productive = actualMin;
  double pct = planned > 0 ? (100.0 * (double)actualMin / (double)planned) : 0.0;
  if (pct > 100.0) pct = 100.0;

  char pctBuf[32];
  snprintf(pctBuf, sizeof(pctBuf), "%.1f", pct);

  return std::string("{\"day\":\"") + day + "\",\"planned_minutes\":" + std::to_string(planned) +
         ",\"actual_minutes\":" + std::to_string(actualMin) +
         ",\"productive_minutes\":" + std::to_string(productive) +
         ",\"effective_focus_minutes\":" + std::to_string(productive) +
         ",\"adherence_pct\":" + pctBuf + ",\"block_count\":" + std::to_string(blockCount) +
         ",\"session_count\":" + std::to_string(sessionCount) + "}";
}
