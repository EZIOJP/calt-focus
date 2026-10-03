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

std::wstring DefaultDbPath() {
  // Prefer CALT_DB; else data/productivity/productivity.db (Focus/Productivity SoT).
  // Legacy CALT_DB pointing at vocab_app.db still works if set explicitly.
  wchar_t* env = _wgetenv(L"CALT_DB");
  if (env && *env) return env;
  std::wstring dir = ExeDir();
  const wchar_t* candidates[] = {
      L"\\..\\..\\..\\..\\..\\data\\productivity\\productivity.db",
      L"\\..\\..\\..\\..\\data\\productivity\\productivity.db",
      L"\\..\\..\\..\\data\\productivity\\productivity.db",
      L"\\..\\..\\data\\productivity\\productivity.db",
      // legacy fallback during transition
      L"\\..\\..\\..\\..\\..\\data\\vocab_app.db",
      L"\\..\\..\\..\\..\\data\\vocab_app.db",
      L"\\..\\..\\..\\data\\vocab_app.db",
      L"\\..\\..\\data\\vocab_app.db",
      nullptr,
  };
  for (int i = 0; candidates[i]; ++i) {
    std::wstring c = dir + candidates[i];
    if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) return c;
  }
  return dir + L"\\..\\..\\..\\..\\..\\data\\productivity\\productivity.db";
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
