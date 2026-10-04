#include "win_service.h"
#include "cmd_gateway.h"
#include "day_loop.h"
#include "day_rollup.h"
#include "focus_watchdog.h"
#include "kill.h"
#include "owner_lock.h"
#include "policy_db.h"
#include "productivity_store.h"
#include "session_db.h"
#include "softland_publish.h"
#include "softland_tick.h"
#include "status_writer.h"
#include "life_content.h"
#include "tracker_agent.h"

#include <windows.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

SERVICE_STATUS gStatus{};
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
HANDLE gStopEvent = nullptr;
std::wstring gDbPath;
std::wstring gLockPath;

std::string gLastKillLine;
std::string gLastKillExe;
FocusWatchState gFocusWatch;

std::string NarrowExe(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

void ReportStatus(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
  gStatus.dwCurrentState = state;
  gStatus.dwWin32ExitCode = exitCode;
  gStatus.dwWaitHint = waitHint;
  gStatus.dwControlsAccepted =
      (state == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
  SetServiceStatus(gStatusHandle, &gStatus);
}

VOID WINAPI ServiceCtrl(DWORD ctrl) {
  if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN) {
    ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
    if (gStopEvent) SetEvent(gStopEvent);
  }
}

bool LockFilePresent(const std::wstring& lockPath) {
  return GetFileAttributesW(lockPath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void PublishStatus(const std::wstring& dbPath, const std::wstring& lockPath,
                   const EnforcerSnapshot& snap) {
  EnforcerStatusSnapshot st;
  st.owns = true;
  st.pid = GetCurrentProcessId();
  st.last_kill = gLastKillLine;
  st.last_kill_exe = gLastKillExe;
  st.lock_present = LockFilePresent(lockPath);
  st.service_running = true;
  st.armed = snap.armed;
  st.locked = snap.locked || snap.incubation;
  st.incubation = snap.incubation;
  st.policy_source = snap.source;
  st.focus_running = gFocusWatch.focus_running;
  st.softland_or_armed = gFocusWatch.softland_or_armed;
  st.focus_relaunch_count = gFocusWatch.focus_relaunch_count;
  st.focus_last_relaunch_at = gFocusWatch.focus_last_relaunch_at;
  st.focus_relaunch_suppressed = gFocusWatch.focus_relaunch_suppressed;
  WriteEnforcerStatus(DefaultStatusJsonPath(dbPath), st);
}

void TickKills(const std::wstring& dbPath, EnforcerSnapshot& snapOut) {
  EnforcerSnapshot snap;
  std::string err;
  if (LoadEnforcerSnapshot(dbPath, snap, err)) {
    snapOut = snap;
    if (snap.armed && (snap.locked || snap.incubation)) {
      std::vector<std::wstring> targets = snap.exes;
      if (snap.anti_tamper && (snap.locked || snap.incubation)) {
        auto extra = AntiTamperExeList();
        targets.insert(targets.end(), extra.begin(), extra.end());
      }
      KillResult kr = KillMatchingExes(targets);
      if (kr.killed > 0 && !kr.last_exe.empty()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char line[256];
        snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u kill pid=%lu exe=", st.wYear,
                 st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 (unsigned long)kr.last_pid);
        gLastKillLine = std::string(line) + NarrowExe(kr.last_exe);
        gLastKillExe = NarrowExe(kr.last_exe);
      }
    }
  } else {
    snapOut = EnforcerSnapshot{};
  }
}

}  // namespace

int RunEnforcerLoop(const std::wstring& dbPath, const std::wstring& lockPath, volatile bool* stop) {
  ClaimOwnerLock(lockPath);
  // LocalSystem (Session 0) cannot see interactive FG — spawn user-session agent.
  const bool useTrackerAgent = TrackerNeedsUserSessionAgent();
  SessionTracker sessions(dbPath);
  DWORD lastBeat = GetTickCount();
  DWORD lastStatus = 0;
  DWORD lastDbSlow = 0;
  DWORD lastAgentEnsure = 0;
  DWORD lastClock = 0;
  // Kill + gateway stay hot. Calendar/plan/DB mirrors are 1-minute work.
  const DWORD kKillMs = 1500;
  const DWORD kClockMs = 5000;       // free_until / incubation expiry
  const DWORD kStatusMs = 10000;     // tray / enforcer_status.json
  const DWORD kLockRefreshMs = 5000;
  const DWORD kDbSlowMs = 60000;     // rollup + plan apply + day-loop + UI mirrors
  const DWORD kAgentEnsureMs = 15000;

  const std::wstring behaviorDir = ProductivityBehaviorDirFromDb(dbPath);
  const std::wstring softlandPath = behaviorDir + L"\\softland_policy.json";
  ProductivityStoreOpen(dbPath);
  ProductivityMigrateAndImport(softlandPath);
  DayLoopEnsureSchema();
  std::wstring dataDir = behaviorDir;
  {
    // One-time Bible JSON → SQLite unlock history (idempotent if tables already filled).
    size_t slash = dataDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
    ProductivityImportLegacyUnlockHistory(dataDir);
  }
  {
    ProductivitySoftland s;
    if (ProductivityLoadSoftland(s)) {
      PublishSoftlandMirror(behaviorDir, s);
    }
  }
  // Focus UI loads from these mirrors — pipe is for writes / Arm / tracking only.
  PublishFocusReadMirrors(behaviorDir);
  CmdGatewayInit();

  while (stop == nullptr || !(*stop)) {
    if (gStopEvent && WaitForSingleObject(gStopEvent, 0) == WAIT_OBJECT_0) break;

    EnforcerSnapshot snap;
    TickKills(dbPath, snap);
    if (useTrackerAgent) {
      DWORD nowEnsure = GetTickCount();
      if (lastAgentEnsure == 0 || nowEnsure - lastAgentEnsure >= kAgentEnsureMs) {
        TrackerAgentEnsure(dbPath);
        lastAgentEnsure = nowEnsure;
      }
    } else {
      sessions.Tick();
    }

    bool softland = false;
    ReadSoftlandEnabled(dbPath, &softland);
    // Watchdog only matters while SoftLand or Arm is on — keep with kill cadence.
    TickFocusWatchdog(snap.armed, softland, gFocusWatch);
    // Writes must stay realtime with kills.
    CmdGatewayPoll(behaviorDir);
    DWORD now = GetTickCount();
    if (lastClock == 0 || now - lastClock >= kClockMs) {
      TickSoftlandClocks(behaviorDir);
      lastClock = now;
    }
    if (lastDbSlow == 0 || now - lastDbSlow >= kDbSlowMs) {
      TickSoftlandPlanAndDayLoop(behaviorDir);
      TickDayRollup(dbPath, behaviorDir);
      PublishFocusReadMirrors(behaviorDir);
      lastDbSlow = now;
    }
    if (now - lastBeat > kLockRefreshMs) {
      RefreshOwnerLock(lockPath);
      lastBeat = now;
    }
    if (lastStatus == 0 || now - lastStatus >= kStatusMs) {
      PublishStatus(dbPath, lockPath, snap);
      lastStatus = now;
    }

    if (gStopEvent) {
      WaitForSingleObject(gStopEvent, kKillMs);
    } else {
      Sleep(kKillMs);
    }
  }
  CmdGatewayShutdown();
  ProductivityStoreClose();
  if (useTrackerAgent) {
    TrackerAgentStop();
  } else {
    sessions.Flush();
  }
  ReleaseOwnerLock(lockPath);
  return 0;
}

VOID WINAPI ServiceMain(DWORD, LPWSTR*) {
  gStatusHandle = RegisterServiceCtrlHandlerW(L"CALTEnforcer", ServiceCtrl);
  if (!gStatusHandle) return;
  gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  ReportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
  gStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ReportStatus(SERVICE_RUNNING);
  volatile bool stop = false;
  RunEnforcerLoop(gDbPath, gLockPath, &stop);
  ReportStatus(SERVICE_STOPPED);
}

int RunAsWindowsService(const std::wstring& dbPath, const std::wstring& lockPath) {
  gDbPath = dbPath;
  gLockPath = lockPath;
  SERVICE_TABLE_ENTRYW table[] = {
      {const_cast<LPWSTR>(L"CALTEnforcer"), ServiceMain},
      {nullptr, nullptr},
  };
  if (!StartServiceCtrlDispatcherW(table)) {
    return (int)GetLastError();
  }
  return 0;
}
