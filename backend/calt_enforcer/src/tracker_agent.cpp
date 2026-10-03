#include "tracker_agent.h"
#include "session_db.h"

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>

#include <cstdio>
#include <string>

namespace {

HANDLE gJob = nullptr;
HANDLE gProc = nullptr;
DWORD gSpawnedSession = 0xFFFFFFFF;

std::wstring ExePath() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::wstring(buf, n);
}

bool ProcessAlive(HANDLE h) {
  if (!h) return false;
  DWORD code = STILL_ACTIVE;
  if (!GetExitCodeProcess(h, &code)) return false;
  return code == STILL_ACTIVE;
}

void CloseAgentHandles() {
  if (gProc) {
    CloseHandle(gProc);
    gProc = nullptr;
  }
  if (gJob) {
    CloseHandle(gJob);
    gJob = nullptr;
  }
  gSpawnedSession = 0xFFFFFFFF;
}

bool SpawnInSession(DWORD sessionId, const std::wstring& dbPath) {
  HANDLE userToken = nullptr;
  if (!WTSQueryUserToken(sessionId, &userToken) || !userToken) return false;

  HANDLE primary = nullptr;
  if (!DuplicateTokenEx(userToken, MAXIMUM_ALLOWED, nullptr, SecurityIdentification, TokenPrimary,
                        &primary) ||
      !primary) {
    CloseHandle(userToken);
    return false;
  }
  CloseHandle(userToken);

  LPVOID env = nullptr;
  SetEnvironmentVariableW(L"CALT_DB", dbPath.c_str());
  if (!CreateEnvironmentBlock(&env, primary, TRUE)) {
    CloseHandle(primary);
    return false;
  }

  std::wstring exe = ExePath();
  // Pass DB on argv — CreateEnvironmentBlock can drop service CALT_DB.
  std::wstring cmdBuf = L"\"" + exe + L"\" --tracker-agent --db \"" + dbPath + L"\"";

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
  PROCESS_INFORMATION pi{};

  BOOL ok = CreateProcessAsUserW(primary, exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                                 CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env, nullptr, &si,
                                 &pi);
  DestroyEnvironmentBlock(env);
  CloseHandle(primary);
  if (!ok) return false;

  // Kill agent when service job closes.
  if (!gJob) {
    gJob = CreateJobObjectW(nullptr, nullptr);
    if (gJob) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
      info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(gJob, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
  }
  if (gJob) AssignProcessToJobObject(gJob, pi.hProcess);

  CloseHandle(pi.hThread);
  if (gProc) CloseHandle(gProc);
  gProc = pi.hProcess;
  gSpawnedSession = sessionId;
  return true;
}

}  // namespace

bool TrackerNeedsUserSessionAgent() {
  DWORD sid = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &sid)) return false;
  return sid == 0;
}

void TrackerAgentStop() {
  if (gProc && ProcessAlive(gProc)) {
    TerminateProcess(gProc, 0);
    WaitForSingleObject(gProc, 3000);
  }
  CloseAgentHandles();
}

void TrackerAgentEnsure(const std::wstring& dbPath) {
  DWORD sessionId = WTSGetActiveConsoleSessionId();
  if (sessionId == 0xFFFFFFFF) {
    TrackerAgentStop();
    return;
  }
  // No interactive user logged on to console.
  HANDLE probe = nullptr;
  if (!WTSQueryUserToken(sessionId, &probe) || !probe) {
    TrackerAgentStop();
    return;
  }
  CloseHandle(probe);

  if (gProc && ProcessAlive(gProc) && gSpawnedSession == sessionId) return;

  TrackerAgentStop();
  SpawnInSession(sessionId, dbPath);
}

int RunTrackerAgentLoop(const std::wstring& dbPath) {
  {
    // One-shot breadcrumb so we can verify spawn + DB path without a debugger.
    size_t slash = dbPath.find_last_of(L"\\/");
    std::wstring behavior =
        (slash == std::wstring::npos) ? L"." : dbPath.substr(0, slash) + L"\\behavior";
    CreateDirectoryW(behavior.c_str(), nullptr);
    std::wstring logPath = behavior + L"\\tracker_agent.log";
    HANDLE hf = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
      char line[512];
      int n = snprintf(line, sizeof(line), "tracker-agent start pid=%lu session db=",
                       (unsigned long)GetCurrentProcessId());
      DWORD w = 0;
      WriteFile(hf, line, (DWORD)n, &w, nullptr);
      // append utf-8 db path
      int nbytes = WideCharToMultiByte(CP_UTF8, 0, dbPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
      if (nbytes > 1) {
        std::string narrow(nbytes - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, dbPath.c_str(), -1, narrow.data(), nbytes, nullptr, nullptr);
        WriteFile(hf, narrow.data(), (DWORD)narrow.size(), &w, nullptr);
      }
      WriteFile(hf, "\r\n", 2, &w, nullptr);
      CloseHandle(hf);
    }
  }

  SessionTracker sessions(dbPath);
  const DWORD kPollMs = 1500;
  DWORD parentCheck = GetTickCount();
  while (true) {
    sessions.Tick();
    Sleep(kPollMs);
    DWORD now = GetTickCount();
    if (now - parentCheck > 15000) {
      parentCheck = now;
      DWORD sid = WTSGetActiveConsoleSessionId();
      DWORD mySid = 0;
      ProcessIdToSessionId(GetCurrentProcessId(), &mySid);
      if (sid == 0xFFFFFFFF || sid != mySid) break;
    }
  }
  sessions.Flush();
  return 0;
}
