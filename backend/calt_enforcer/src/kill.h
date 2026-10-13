#pragma once
#include <string>
#include <vector>

struct KillResult {
  int killed = 0;
  std::wstring last_exe;
  unsigned long last_pid = 0;
};

// Kill processes whose image name matches any of exes (case-insensitive).
// toastSuffix is the balloon tail after "Closed: <exe> — ". Null keeps the Arm wording.
KillResult KillMatchingExes(const std::vector<std::wstring>& exes,
                            const wchar_t* toastSuffix = nullptr);
