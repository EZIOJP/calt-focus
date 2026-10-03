#include "owner_lock.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

void EnsureParentDir(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return;
  std::wstring dir = path.substr(0, slash);
  CreateDirectoryW(dir.c_str(), nullptr);
}

}  // namespace

void ClaimOwnerLock(const std::wstring& lockPath) {
  EnsureParentDir(lockPath);
  DWORD pid = GetCurrentProcessId();
  auto now = std::chrono::duration<double>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, lockPath.c_str(), L"wb");
#else
  f = _wfopen(lockPath.c_str(), L"wb");
#endif
  if (!f) return;
  fprintf(f, "%lu\n%.3f\n", (unsigned long)pid, now);
  fclose(f);
}

void RefreshOwnerLock(const std::wstring& lockPath) { ClaimOwnerLock(lockPath); }

void ReleaseOwnerLock(const std::wstring& lockPath) {
  DeleteFileW(lockPath.c_str());
}
