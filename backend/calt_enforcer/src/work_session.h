#pragma once

#include <string>

struct WorkSessionTickResult {
  int killed = 0;
  std::wstring last_exe;
  unsigned long last_pid = 0;
};

// While a work session is active, close the foreground app unless it was chosen.
// Websites are enforced separately by calt_msg_host reading the same file.
WorkSessionTickResult TickWorkSession(const std::wstring& behaviorDir);

// Gateway op session.set. Returns "" or an error token. mirrorJson is the written document.
std::string WorkSessionHandleSet(const std::wstring& behaviorDir, const std::string& payload,
                                 std::string* mirrorJson);
