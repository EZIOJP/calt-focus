#include "session_db.h"
#include "foreground.h"
#include "sqlite3.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::string IsoUtcFromMs(ULONGLONG ms) {
  FILETIME ft;
  ULARGE_INTEGER uli;
  uli.QuadPart = (ms + 11644473600000ULL) * 10000ULL;
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
  return buf;
}

bool IsBrowserExe(const std::wstring& exe) {
  // Broad browser coverage — CALT original list (not Cold Turkey).
  // Match basename tokens so we skip FG session writes (SelfTracker owns tabs).
  static const wchar_t* kBrowsers[] = {
      L"chrome",    L"msedge",     L"firefox",   L"brave",     L"opera",
      L"vivaldi",   L"arc",        L"zen",       L"sidekick",  L"librewolf",
      L"waterfox",  L"floorp",     L"thorium",   L"chromium",  L"iexplore",
      L"duckduckgo", L"avastbrowser", L"avg",
      nullptr};
  std::wstring lower = exe;
  for (auto& c : lower) c = (wchar_t)towlower(c);
  for (int i = 0; kBrowsers[i]; ++i) {
    if (lower.find(kBrowsers[i]) != std::wstring::npos) return true;
  }
  return false;
}

std::wstring SiteFromTitle(const std::wstring& title) {
  // Best-effort: last segment after " - " / em-dash (same idea as Python normalize).
  size_t pos = title.find_last_of(L"-–—");
  if (pos != std::wstring::npos && pos + 1 < title.size()) {
    std::wstring hint = title.substr(pos + 1);
    while (!hint.empty() && hint[0] == L' ') hint.erase(hint.begin());
    if (hint.size() > 2) {
      for (auto& c : hint) c = (wchar_t)towlower(c);
      // strip spaces for key stability
      std::wstring out;
      for (wchar_t c : hint) {
        if (c != L' ') out.push_back(c);
      }
      if (!out.empty()) return out;
    }
  }
  return L"unknown";
}

}  // namespace

SessionTracker::SessionTracker(std::wstring dbPath) : dbPath_(std::move(dbPath)) {}

ULONGLONG SessionTracker::WallClockMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return (uli.QuadPart / 10000ULL) - 11644473600000ULL;
}

ULONGLONG SessionTracker::IdleMs() {
  // Must use interactive-session last-input — Session 0 GetLastInputInfo
  // looks permanently idle and blocks all FG tracking under the service.
  return GetInteractiveIdleMs();
}

std::wstring SessionTracker::GroupKey(const std::wstring& exe, const std::wstring& title) {
  if (IsBrowserExe(exe)) return exe + L"|" + SiteFromTitle(title);
  return exe;
}

int SessionTracker::ResolveUserId() {
  if (userId_ > 0) return userId_;
  sqlite3* db = nullptr;
  std::string path = Narrow(dbPath_);
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    // Productivity DB is local-first single-user; still allow inserts if DB briefly locked.
    userId_ = 1;
    return userId_;
  }
  sqlite3_busy_timeout(db, 3000);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT user_id FROM enforcer_runtime ORDER BY updated_at DESC LIMIT 1;",
                         -1, &st, nullptr) == SQLITE_OK) {
    if (sqlite3_step(st) == SQLITE_ROW) userId_ = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
  }
  if (userId_ <= 0) {
    // Optional legacy Study table — may be absent on productivity.db.
    if (sqlite3_prepare_v2(db, "SELECT id FROM users ORDER BY id LIMIT 1;", -1, &st, nullptr) ==
        SQLITE_OK) {
      if (sqlite3_step(st) == SQLITE_ROW) userId_ = sqlite3_column_int(st, 0);
      sqlite3_finalize(st);
    }
  }
  sqlite3_close(db);
  if (userId_ <= 0) userId_ = 1;  // Focus/Productivity default
  return userId_;
}

bool SessionTracker::InsertSession(const std::wstring& exe, const std::wstring& title,
                                   ULONGLONG startMs, ULONGLONG endMs) {
  if (endMs < startMs + kMinSessionMs) return false;
  int uid = ResolveUserId();
  if (uid <= 0) return false;

  sqlite3* db = nullptr;
  std::string path = Narrow(dbPath_);
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return false;
  }
  sqlite3_busy_timeout(db, 5000);
  sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

  const char* sql =
      "INSERT INTO tracked_sessions "
      "(session_id, user_id, task_id, start_time, end_time, source, category, "
      " window_title, app_name, category_source, category_before_llm, override_productive) "
      "VALUES (?, ?, NULL, ?, ?, 'desktop_tracker', NULL, ?, ?, 'native', NULL, NULL);";

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  ++counter_;
  char sid[80];
  snprintf(sid, sizeof(sid), "native-%llu-%lu-%u", (unsigned long long)endMs,
           (unsigned long)GetCurrentProcessId(), counter_);

  std::string startIso = IsoUtcFromMs(startMs);
  std::string endIso = IsoUtcFromMs(endMs);
  std::string app = Narrow(exe);
  std::string tit = Narrow(title);

  sqlite3_bind_text(st, 1, sid, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, uid);
  sqlite3_bind_text(st, 3, startIso.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, endIso.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, tit.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 6, app.c_str(), -1, SQLITE_TRANSIENT);

  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  sqlite3_close(db);
  return ok;
}

void SessionTracker::CloseCurrent(ULONGLONG endMs) {
  if (curExe_.empty() || startedMs_ == 0) return;
  InsertSession(curExe_, curTitle_, startedMs_, endMs);
  curExe_.clear();
  curTitle_.clear();
  curGroup_.clear();
  curPid_ = 0;
  startedMs_ = 0;
}

void SessionTracker::Begin(const std::wstring& exe, const std::wstring& title, DWORD pid,
                           ULONGLONG nowMs) {
  curExe_ = exe;
  curTitle_ = title;
  curGroup_ = GroupKey(exe, title);
  curPid_ = pid;
  startedMs_ = nowMs;
  idle_ = false;
}

void SessionTracker::Tick() {
  ULONGLONG now = WallClockMs();

  // Sleep / hibernate: wall clock jumped — close at last known tick (Python sleep_gap).
  if (lastTickMs_ > 0 && now > lastTickMs_ + kSleepGapMs && !curExe_.empty()) {
    CloseCurrent(lastTickMs_);
  }
  lastTickMs_ = now;

  // Idle: stop attributing time to FG app (Python idle_threshold).
  ULONGLONG idle = IdleMs();
  if (idle >= kIdleMs) {
    if (!idle_ && !curExe_.empty()) {
      ULONGLONG endAt = (now > idle) ? (now - idle) : now;
      if (endAt < startedMs_) endAt = startedMs_ + kMinSessionMs;
      CloseCurrent(endAt);
      idle_ = true;
    }
    return;
  }
  if (idle_) idle_ = false;

  ForegroundInfo fg;
  if (!GetForegroundInfo(fg)) {
    // Lock screen / no FG — flush like idle.
    if (!curExe_.empty()) CloseCurrent(now);
    return;
  }

  // Browsers: SelfTracker extension owns active-tab URL sessions — skip OS shell time.
  if (IsBrowserExe(fg.exe)) {
    if (!curExe_.empty()) CloseCurrent(now);
    return;
  }

  std::wstring group = GroupKey(fg.exe, fg.title);

  if (curExe_.empty()) {
    Begin(fg.exe, fg.title, fg.pid, now);
    return;
  }

  // Max session chunk (Python max_session_s).
  if (now >= startedMs_ + kMaxSessionMs) {
    CloseCurrent(now);
    Begin(fg.exe, fg.title, fg.pid, now);
    return;
  }

  if (group == curGroup_ && fg.pid == curPid_) {
    if (!fg.title.empty()) curTitle_ = fg.title;
    return;
  }

  CloseCurrent(now);
  Begin(fg.exe, fg.title, fg.pid, now);
}

void SessionTracker::Flush() {
  if (curExe_.empty() || startedMs_ == 0) return;
  CloseCurrent(WallClockMs());
}
