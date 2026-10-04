#include "device_block.h"
#include "tracker_agent.h"
#include "win_service.h"

#include <windows.h>

#include <iostream>
#include <string>

namespace {

std::wstring ExeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p(buf, n);
  size_t slash = p.find_last_of(L"\\/");
  return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}

std::wstring ReadUtf8FileTrim(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  char buf[1024];
  DWORD n = 0;
  const BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &n, nullptr);
  CloseHandle(h);
  if (!ok || n == 0) return {};
  buf[n] = 0;
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
    buf[--n] = 0;
  }
  if (n == 0) return {};
  int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, (int)n, nullptr, 0);
  if (wlen <= 0) return {};
  std::wstring out(static_cast<size_t>(wlen), 0);
  MultiByteToWideChar(CP_UTF8, 0, buf, (int)n, out.data(), wlen);
  return out;
}

std::wstring MachineEnv(const wchar_t* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
                    0, KEY_READ, &key) != ERROR_SUCCESS) {
    return {};
  }
  wchar_t buf[1024];
  DWORD type = 0;
  DWORD cb = sizeof(buf);
  const LONG rc = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || !buf[0]) return {};
  return buf;
}

std::wstring DefaultDbPath() {
  // Prefer CALT_DB process env; then Machine env (service/installer); then
  // ProgramData sidecar written by install_native_enforcer.ps1; then walk up
  // from the exe for the nearest Focus productivity.db.
  wchar_t* env = _wgetenv(L"CALT_DB");
  if (env && *env) return env;
  const std::wstring machine = MachineEnv(L"CALT_DB");
  if (!machine.empty() && GetFileAttributesW(machine.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return machine;
  }
  const std::wstring sidecar = ExeDir() + L"\\calt_db.path";
  const std::wstring fromSidecar = ReadUtf8FileTrim(sidecar);
  if (!fromSidecar.empty() &&
      GetFileAttributesW(fromSidecar.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return fromSidecar;
  }
  std::wstring cur = ExeDir();
  for (int i = 0; i < 8; ++i) {
    const std::wstring prod = cur + L"\\data\\productivity\\productivity.db";
    if (GetFileAttributesW(prod.c_str()) != INVALID_FILE_ATTRIBUTES) return prod;
    const std::wstring legacy = cur + L"\\data\\vocab_app.db";
    if (GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) return legacy;
    const size_t slash = cur.find_last_of(L"\\/");
    if (slash == std::wstring::npos) break;
    cur = cur.substr(0, slash);
  }
  return ExeDir() + L"\\..\\..\\..\\data\\productivity\\productivity.db";
}

std::wstring DefaultLockPath(const std::wstring& dbPath) {
  wchar_t* env = _wgetenv(L"CALT_ENFORCER_LOCK");
  if (env && *env) return env;
  // sibling of db: data/productivity/behavior/enforcer_owner.lock
  size_t slash = dbPath.find_last_of(L"\\/");
  std::wstring dataDir = (slash == std::wstring::npos) ? L"." : dbPath.substr(0, slash);
  return dataDir + L"\\behavior\\enforcer_owner.lock";
}

std::wstring BehaviorDirFromDb(const std::wstring& dbPath) {
  size_t slash = dbPath.find_last_of(L"\\/");
  std::wstring dataDir = (slash == std::wstring::npos) ? L"." : dbPath.substr(0, slash);
  return dataDir + L"\\behavior";
}

void PrintUsage() {
  std::wcout
      << L"CALT Native Enforcer (C++) — zero-Python desktop tracker\n"
      << L"  calt_enforcer.exe              console loop\n"
      << L"  calt_enforcer.exe --service    Windows Service dispatcher\n"
      << L"  calt_enforcer.exe --tracker-agent  user-session FG tracker (spawned by service)\n"
      << L"  calt_enforcer.exe --device-block-apply --confirm \"DEVICE LOCK\"\n"
      << L"  calt_enforcer.exe --device-block-remove --confirm \"DEVICE LOCK\"\n"
      << L"  calt_enforcer.exe --device-block-refresh [--force]\n"
      << L"Env: CALT_DB (default data/productivity/productivity.db), CALT_ENFORCER_LOCK\n"
      << L"Policy mirrors: <db-dir>/behavior/enforcer_policy.json\n"
      << L"Status: <db-dir>/behavior/enforcer_status.json\n"
      << L"Bible corpus: <db-dir>/bible/\n"
      << L"No Python runtime required. No Cold Turkey code.\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-h") == 0) {
      PrintUsage();
      return 0;
    }
  }

  std::wstring db = DefaultDbPath();
  std::wstring lock = DefaultLockPath(db);
  std::wstring behavior = BehaviorDirFromDb(db);

  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--service") == 0) {
      return RunAsWindowsService(db, lock);
    }
  }

  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--tracker-agent") == 0) {
      std::wstring agentDb = db;
      for (int j = 1; j < argc; ++j) {
        if (wcscmp(argv[j], L"--db") == 0 && j + 1 < argc) {
          agentDb = argv[j + 1];
          break;
        }
      }
      return RunTrackerAgentLoop(agentDb);
    }
  }

  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--device-block-apply") == 0 ||
        wcscmp(argv[i], L"--device-block-remove") == 0 ||
        wcscmp(argv[i], L"--device-block-refresh") == 0) {
      return DeviceBlockCliMain(behavior, argc, argv);
    }
  }

  std::wcout << L"CALT enforcer console. DB=" << db << L"\n";
  volatile bool stop = false;
  SetConsoleCtrlHandler(
      [](DWORD) -> BOOL {
        return TRUE;  // allow clean Ctrl+C via process kill; loop exits on close
      },
      TRUE);
  return RunEnforcerLoop(db, lock, &stop);
}
