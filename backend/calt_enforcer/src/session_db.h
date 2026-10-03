#pragma once
#include <windows.h>
#include <string>

// Foreground session writer → tracked_sessions (same SQLite as Python).
// Mirrors CALT Python TrackerService lifecycle: idle / sleep_gap / max_session.
class SessionTracker {
 public:
  explicit SessionTracker(std::wstring dbPath);

  void Tick();
  void Flush();

 private:
  std::wstring dbPath_;
  std::wstring curExe_;
  std::wstring curTitle_;
  std::wstring curGroup_;  // exe or exe|site for browsers
  DWORD curPid_ = 0;
  ULONGLONG startedMs_ = 0;
  ULONGLONG lastTickMs_ = 0;
  bool idle_ = false;
  int userId_ = 0;
  unsigned counter_ = 0;

  // Defaults match backend/behavior/tracker_storage.py TrackerConfig
  static constexpr ULONGLONG kIdleMs = 300000;       // 300s
  static constexpr ULONGLONG kSleepGapMs = 60000;     // 60s
  static constexpr ULONGLONG kMaxSessionMs = 600000;  // 600s
  static constexpr ULONGLONG kMinSessionMs = 2000;

  int ResolveUserId();
  bool InsertSession(const std::wstring& exe, const std::wstring& title, ULONGLONG startMs,
                     ULONGLONG endMs);
  void CloseCurrent(ULONGLONG endMs);
  void Begin(const std::wstring& exe, const std::wstring& title, DWORD pid, ULONGLONG nowMs);
  static ULONGLONG WallClockMs();
  static ULONGLONG IdleMs();
  static std::wstring GroupKey(const std::wstring& exe, const std::wstring& title);
};
