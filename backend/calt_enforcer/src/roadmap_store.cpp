#include "roadmap_store.h"

#include "productivity_store.h"
#include "softland_publish.h"
#include "sqlite3.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::string IsoLocalNow() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

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
          char hex[8];
          snprintf(hex, sizeof(hex), "\\u%04x", c);
          o += hex;
        } else {
          o.push_back(static_cast<char>(c));
        }
    }
  }
  return o;
}

int JsonIntNear(const std::string& body, const char* key, int fallback) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return fallback;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return fallback;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size()) return fallback;
  return atoi(body.c_str() + i);
}

}  // namespace

void RoadmapEnsureTables() {
  if (!gDb) return;
  sqlite3_exec(gDb,
               "CREATE TABLE IF NOT EXISTS productivity_roadmap_state ("
               "  user_id INTEGER PRIMARY KEY,"
               "  document_json TEXT NOT NULL,"
               "  updated_at TEXT NOT NULL"
               ");"
               "CREATE TABLE IF NOT EXISTS productivity_roadmap_attempts ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  user_id INTEGER NOT NULL,"
               "  at TEXT NOT NULL,"
               "  topic_id TEXT,"
               "  level INTEGER,"
               "  correct INTEGER NOT NULL,"
               "  time_ms INTEGER,"
               "  mode TEXT,"
               "  prompt TEXT"
               ");"
               "CREATE INDEX IF NOT EXISTS ix_roadmap_attempts_user_at "
               "  ON productivity_roadmap_attempts(user_id, at DESC);",
               nullptr, nullptr, nullptr);
}

std::string RoadmapGetDocumentJson(int userId) {
  RoadmapEnsureTables();
  if (!gDb) return "{}";
  if (userId <= 0) userId = 1;
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT document_json FROM productivity_roadmap_state WHERE user_id=? LIMIT 1;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return "{}";
  sqlite3_bind_int(st, 1, userId);
  std::string out = "{}";
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(st, 0);
    if (t) out.assign(reinterpret_cast<const char*>(t));
  }
  sqlite3_finalize(st);
  return out.empty() ? "{}" : out;
}

bool RoadmapSaveDocument(int userId, const std::string& documentJson,
                         const std::wstring& behaviorDir) {
  RoadmapEnsureTables();
  if (!gDb || documentJson.empty() || documentJson[0] != '{') return false;
  if (userId <= 0) userId = 1;
  const std::string now = IsoLocalNow();
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "INSERT INTO productivity_roadmap_state(user_id, document_json, updated_at) "
      "VALUES(?,?,?) "
      "ON CONFLICT(user_id) DO UPDATE SET "
      "document_json=excluded.document_json, updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, documentJson.c_str(), (int)documentJson.size(), SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, now.c_str(), (int)now.size(), SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  if (!ok) return false;

  // Mirror for quick FE / file checks (includes summary fields).
  std::string mirror = std::string("{\"updated_at\":\"") + now + "\",\"user_id\":" +
                       std::to_string(userId) + ",\"document\":" + documentJson +
                       ",\"summary\":" + RoadmapSummaryJson(userId) + "}";
  if (!behaviorDir.empty()) {
    WriteBehaviorMirrorFile(behaviorDir, L"jp_roadmap.json", mirror);
  }
  return true;
}

bool RoadmapLogAttempt(int userId, const std::string& topicId, int level, bool correct,
                       int timeMs, const std::string& mode, const std::string& prompt) {
  RoadmapEnsureTables();
  if (!gDb) return false;
  if (userId <= 0) userId = 1;
  const std::string now = IsoLocalNow();
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "INSERT INTO productivity_roadmap_attempts"
      "(user_id, at, topic_id, level, correct, time_ms, mode, prompt) "
      "VALUES(?,?,?,?,?,?,?,?);";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, now.c_str(), (int)now.size(), SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, topicId.c_str(), (int)topicId.size(), SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 4, level);
  sqlite3_bind_int(st, 5, correct ? 1 : 0);
  sqlite3_bind_int(st, 6, timeMs < 0 ? 0 : timeMs);
  sqlite3_bind_text(st, 7, mode.c_str(), (int)mode.size(), SQLITE_TRANSIENT);
  // Cap prompt length so a runaway string can't bloat the DB.
  std::string p = prompt.size() > 400 ? prompt.substr(0, 400) : prompt;
  sqlite3_bind_text(st, 8, p.c_str(), (int)p.size(), SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

std::string RoadmapRecentAttemptsJson(int limit, int userId) {
  RoadmapEnsureTables();
  if (!gDb) return "[]";
  if (userId <= 0) userId = 1;
  if (limit <= 0) limit = 20;
  if (limit > 200) limit = 200;
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT id, at, topic_id, level, correct, time_ms, mode, prompt "
      "FROM productivity_roadmap_attempts WHERE user_id=? "
      "ORDER BY id DESC LIMIT ?;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return "[]";
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_int(st, 2, limit);
  std::string out = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    if (!first) out += ",";
    first = false;
    const char* at = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    const char* topic = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    const char* mode = reinterpret_cast<const char*>(sqlite3_column_text(st, 6));
    const char* prompt = reinterpret_cast<const char*>(sqlite3_column_text(st, 7));
    out += "{\"id\":" + std::to_string(sqlite3_column_int64(st, 0)) + ",\"at\":\"" +
           JsonEscape(at ? at : "") + "\",\"topic_id\":\"" + JsonEscape(topic ? topic : "") +
           "\",\"level\":" + std::to_string(sqlite3_column_int(st, 3)) + ",\"correct\":" +
           (sqlite3_column_int(st, 4) ? "true" : "false") + ",\"time_ms\":" +
           std::to_string(sqlite3_column_int(st, 5)) + ",\"mode\":\"" +
           JsonEscape(mode ? mode : "") + "\",\"prompt\":\"" + JsonEscape(prompt ? prompt : "") +
           "\"}";
  }
  sqlite3_finalize(st);
  out += "]";
  return out;
}

std::string RoadmapSummaryJson(int userId) {
  const std::string doc = RoadmapGetDocumentJson(userId);
  const int totalQ = JsonIntNear(doc, "totalQuestionsAnswered", 0);
  const int totalC = JsonIntNear(doc, "totalCorrectAnswered", 0);
  const int xp = JsonIntNear(doc, "userXP", 0);
  const int streak = JsonIntNear(doc, "dailyStreak", 0);
  const int practiceSecs = JsonIntNear(doc, "totalPracticeSeconds", 0);

  int attemptRows = 0;
  if (gDb) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(gDb,
                           "SELECT COUNT(*) FROM productivity_roadmap_attempts WHERE user_id=?;", -1,
                           &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_int(st, 1, userId <= 0 ? 1 : userId);
      if (sqlite3_step(st) == SQLITE_ROW) attemptRows = sqlite3_column_int(st, 0);
      sqlite3_finalize(st);
    }
  }

  return std::string("{\"total_questions\":") + std::to_string(totalQ) + ",\"total_correct\":" +
         std::to_string(totalC) + ",\"xp\":" + std::to_string(xp) + ",\"streak\":" +
         std::to_string(streak) + ",\"practice_seconds\":" + std::to_string(practiceSecs) +
         ",\"attempt_rows\":" + std::to_string(attemptRows) + "}";
}
