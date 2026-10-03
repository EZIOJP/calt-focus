#pragma once
#include <string>
#include <vector>

struct KillResult {
  int killed = 0;
  std::wstring last_exe;
  unsigned long last_pid = 0;
};

// Kill processes whose image name matches any of exes (case-insensitive).
KillResult KillMatchingExes(const std::vector<std::wstring>& exes);
