#include "track_tab.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>

// Prefer amalgamation next to enforcer; fallback compile without if missing at link.
#include "../../calt_enforcer/third_party/sqlite/sqlite3.h"

namespace {

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

std::string IsoUtcNow() {
  SYSTEMTIME st;
  GetSystemTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.000Z", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
  return buf;
}

/** Add seconds to an ISO-Z stamp (second resolution). Supports negative offsets. */
std::string IsoUtcAddSeconds(const std::string& iso, int seconds) {
  // Parse YYYY-MM-DDTHH:MM:SS
  if (iso.size() < 19) return IsoUtcNow();
  SYSTEMTIME st{};
  st.wYear = (WORD)atoi(iso.c_str());
  st.wMonth = (WORD)atoi(iso.c_str() + 5);
  st.wDay = (WORD)atoi(iso.c_str() + 8);
  st.wHour = (WORD)atoi(iso.c_str() + 11);
  st.wMinute = (WORD)atoi(iso.c_str() + 14);
  st.wSecond = (WORD)atoi(iso.c_str() + 17);
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) return IsoUtcNow();
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  long long delta = (long long)seconds * 10000000LL;
  long long next = (long long)uli.QuadPart + delta;
  if (next < 0) next = 0;
  uli.QuadPart = (unsigned long long)next;
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  SYSTEMTIME out{};
  FileTimeToSystemTime(&ft, &out);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.000Z", out.wYear, out.wMonth, out.wDay,
           out.wHour, out.wMinute, out.wSecond);
  return buf;
}

int ResolveUserId(sqlite3* db) {
  int uid = 0;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT user_id FROM enforcer_runtime ORDER BY updated_at DESC LIMIT 1;",
                         -1, &st, nullptr) == SQLITE_OK) {
    if (sqlite3_step(st) == SQLITE_ROW) uid = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
  }
  if (uid <= 0) {
    if (sqlite3_prepare_v2(db, "SELECT id FROM users ORDER BY id LIMIT 1;", -1, &st, nullptr) ==
        SQLITE_OK) {
      if (sqlite3_step(st) == SQLITE_ROW) uid = sqlite3_column_int(st, 0);
      sqlite3_finalize(st);
    }
  }
  return uid;
}

/** Extend an open selftracker row for the same app if last activity was recent (≤120s). */
bool TryExtendSession(sqlite3* db, int uid, const std::string& app, const std::string& title,
                      const std::string& now) {
  // now-120s as lower bound (lexicographic ISO-Z works for same-length stamps)
  std::string since = IsoUtcAddSeconds(now, -120);
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT session_id, end_time FROM tracked_sessions "
      "WHERE user_id=? AND source='selftracker' AND app_name=? AND end_time>=? "
      "ORDER BY start_time DESC LIMIT 1;";
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, uid);
  sqlite3_bind_text(st, 2, app.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, since.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_ROW) {
    sqlite3_finalize(st);
    return false;
  }
  const char* sid = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
  const char* endPrev = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
  std::string sessionId = sid ? sid : "";
  std::string prevEnd = endPrev ? endPrev : "";
  sqlite3_finalize(st);
  if (sessionId.empty()) return false;

  std::string newEnd = IsoUtcAddSeconds(now, 20);
  // Never shorten an already-longer cushion from the initial insert.
  if (!prevEnd.empty() && prevEnd > newEnd) newEnd = prevEnd;
  sqlite3_stmt* up = nullptr;
  if (sqlite3_prepare_v2(db,
                         "UPDATE tracked_sessions SET end_time=?, window_title=? WHERE session_id=?;",
                         -1, &up, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(up, 1, newEnd.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(up, 2, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(up, 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(up) == SQLITE_DONE && sqlite3_changes(db) > 0;
  sqlite3_finalize(up);
  return ok;
}

}  // namespace

std::wstring ResolveCaltDbPathW() {
  wchar_t* env = _wgetenv(L"CALT_DB");
  if (env && *env) return env;
  wchar_t* repo = _wgetenv(L"CALT_REPO");
  if (repo && *repo) {
    std::wstring p = Join(repo, L"data\\productivity\\productivity.db");
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    p = Join(repo, L"data\\vocab_app.db");
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
  }
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (!n || n >= MAX_PATH) return L"";
  std::wstring dir = DirOf(buf);
  for (int i = 0; i < 8; ++i) {
    std::wstring cand = Join(dir, L"data\\productivity\\productivity.db");
    if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) return cand;
    cand = Join(dir, L"data\\vocab_app.db");
    if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) return cand;
    dir = DirOf(dir);
  }
  return L"";
}

bool TrackTabToSqlite(const std::string& url, const std::string& title, const std::string& domain) {
  std::wstring dbPath = ResolveCaltDbPathW();
  if (dbPath.empty()) return false;

  sqlite3* db = nullptr;
  std::string path = Narrow(dbPath);
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return false;
  }
  sqlite3_busy_timeout(db, 5000);
  sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

  int uid = ResolveUserId(db);
  if (uid <= 0) {
    sqlite3_close(db);
    return false;
  }

  std::string now = IsoUtcNow();
  std::string app = domain.empty() ? "msedge.exe" : ("msedge:" + domain);
  std::string tit = title.empty() ? url : title;
  if (tit.size() > 200) tit.resize(200);

  // Heartbeat: extend the open row so Focus calendar gets real duration (≥45s).
  if (TryExtendSession(db, uid, app, tit, now)) {
    sqlite3_close(db);
    return true;
  }

  // New visit: start now, end = now+60s so a single hit clears the 45s calendar floor.
  std::string end = IsoUtcAddSeconds(now, 60);
  const char* sql =
      "INSERT INTO tracked_sessions "
      "(session_id, user_id, task_id, start_time, end_time, source, category, "
      " window_title, app_name, category_source, category_before_llm, override_productive) "
      "VALUES (?, ?, NULL, ?, ?, 'selftracker', NULL, ?, ?, 'native_msg_host', NULL, NULL);";

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  char sid[96];
  snprintf(sid, sizeof(sid), "selftracker-%lu-%s", (unsigned long)GetTickCount(),
           domain.empty() ? "tab" : domain.c_str());

  sqlite3_bind_text(st, 1, sid, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, uid);
  sqlite3_bind_text(st, 3, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, end.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, tit.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 6, app.c_str(), -1, SQLITE_TRANSIENT);

  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  sqlite3_close(db);
  return ok;
}
