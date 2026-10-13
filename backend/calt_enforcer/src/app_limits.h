#pragma once

#include <string>

struct AppLimitTickResult {
  int killed = 0;
  std::wstring last_exe;
  unsigned long last_pid = 0;
};

// Read app_limits.json, add today's foreground time, kill exes whose budget is spent.
// Independent of Arm. A free day does not restore a spent budget. Resets at local midnight.
AppLimitTickResult TickAppTimeLimits(const std::wstring& dbPath, const std::wstring& behaviorDir);

// Gateway op app_limits.set. Returns "" or an error token. mirrorJson is the written document.
std::string AppLimitsHandleSet(const std::wstring& behaviorDir, const std::wstring& dbPath,
                               const std::string& payload, std::string* mirrorJson);
