#pragma once
#include <string>

struct EnforcerStatusSnapshot {
  bool owns = true;
  unsigned long pid = 0;
  std::string last_kill;       // human line
  std::string last_kill_exe;   // basename
  bool lock_present = false;
  bool service_running = true;  // this process is the enforcer
  bool armed = false;
  bool locked = false;
  bool incubation = false;
  std::string policy_source;
  bool focus_running = false;
  bool softland_or_armed = false;
  unsigned focus_relaunch_count = 0;
  std::string focus_last_relaunch_at;
  bool focus_relaunch_suppressed = false;
};

std::wstring DefaultStatusJsonPath(const std::wstring& dbPath);

// Write data/behavior/enforcer_status.json (no Python).
bool WriteEnforcerStatus(const std::wstring& statusPath, const EnforcerStatusSnapshot& snap);
