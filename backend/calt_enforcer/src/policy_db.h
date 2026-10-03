#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct EnforcerSnapshot {
  bool armed = false;
  bool locked = false;
  bool incubation = false;
  bool anti_tamper = true;
  std::string lock_mode;  // none | timer | password | phrase
  std::int64_t lock_until_unix = 0;
  std::string unlock_password;
  std::string unlock_phrase;
  std::string provided_unlock;
  std::vector<std::wstring> exes;
  bool ok = false;
  std::string source;  // "sqlite" | "json" | "sqlite+json"
};

// Paths next to DB: <data>/behavior/enforcer_policy.json
std::wstring DefaultPolicyJsonPath(const std::wstring& dbPath);

// Atomic write of enforcer_policy.json (Arm SoT mirror for kill loop).
bool WriteEnforcerPolicyJson(const std::wstring& jsonPath, const EnforcerSnapshot& snap,
                             bool anti_tamper, bool protect_uninstall);

/** Best-effort mirror of Arm snapshot into enforcer_runtime (avoids stale SQLite reads). */
bool SyncEnforcerRuntimeSqlite(const std::wstring& dbPath, const EnforcerSnapshot& snap);

// Load policy with ZERO Python dependency:
// 1) SQLite enforcer_runtime (may be stale leftover from web backend)
// 2) Overlay/fallback data/behavior/enforcer_policy.json when present
// Strong locks: timer/password/phrase force armed+locked until satisfied.
bool LoadEnforcerSnapshot(const std::wstring& dbPath, EnforcerSnapshot& out, std::string& err);

// Extra kill targets while locked + anti_tamper (Task Manager, time tools, etc.).
std::vector<std::wstring> AntiTamperExeList();
