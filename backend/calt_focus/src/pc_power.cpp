#include "pc_power.h"

#include <windows.h>
#include <powrprof.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "powrprof.lib")

namespace {

std::string EscapeJson(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"')
      o += "\\\"";
    else if (c == '\\')
      o += "\\\\";
    else if (c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      o += buf;
    } else
      o.push_back((char)c);
  }
  return o;
}

bool RunShutdownArgs(const wchar_t* args, std::string* err) {
  wchar_t sys[MAX_PATH]{};
  GetSystemDirectoryW(sys, MAX_PATH);
  std::wstring exe = std::wstring(sys) + L"\\shutdown.exe";
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmd;
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
    if (err) *err = "create_process_failed";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

}  // namespace

PcPowerResult PcPowerRun(PcPowerOp op) {
  PcPowerResult r;
  switch (op) {
    case PcPowerOp::Lock: {
      if (LockWorkStation()) {
        r.ok = true;
        r.detail = "locked";
      } else {
        r.detail = "lock_failed";
      }
      break;
    }
    case PcPowerOp::Sleep: {
      // FALSE hibernate, FALSE forceCritical — soft suspend.
      if (SetSuspendState(FALSE, FALSE, FALSE)) {
        r.ok = true;
        r.detail = "sleeping";
      } else {
        r.detail = "sleep_failed";
      }
      break;
    }
    case PcPowerOp::Shutdown: {
      return PcPowerShutdownIn(30);
    }
    case PcPowerOp::Cancel: {
      std::string err;
      if (RunShutdownArgs(L"/a", &err)) {
        r.ok = true;
        r.detail = "shutdown cancelled";
      } else {
        r.detail = err.empty() ? "cancel_failed" : err;
      }
      break;
    }
  }
  return r;
}

PcPowerResult PcPowerShutdownIn(unsigned seconds) {
  PcPowerResult r;
  if (seconds > 86400) seconds = 86400;
  std::wstring args = L"/s /t " + std::to_wstring(seconds) + L" /c \"CALT Focus\"";
  std::string err;
  if (RunShutdownArgs(args.c_str(), &err)) {
    r.ok = true;
    char buf[96];
    snprintf(buf, sizeof(buf), "shutdown in %us — type shutdown cancel to abort", seconds);
    r.detail = buf;
  } else {
    r.detail = err.empty() ? "shutdown_failed" : err;
  }
  return r;
}

std::string PcPowerResultJson(const PcPowerResult& r) {
  return std::string("{\"ok\":") + (r.ok ? "true" : "false") + ",\"detail\":\"" +
         EscapeJson(r.detail) + "\"}";
}
