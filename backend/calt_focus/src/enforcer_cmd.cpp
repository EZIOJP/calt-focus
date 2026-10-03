#include "enforcer_cmd.h"

#include <windows.h>

#include <string>

namespace {

bool SendOnce(const std::string& jsonReq, std::string& jsonResp, unsigned timeoutMs) {
  jsonResp.clear();
  HANDLE pipe = CreateFileW(L"\\\\.\\pipe\\calt_enforcer_cmd", GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    // 231 = ERROR_PIPE_BUSY — wait for a free instance.
    if (err == ERROR_PIPE_BUSY || err == ERROR_FILE_NOT_FOUND) {
      if (!WaitNamedPipeW(L"\\\\.\\pipe\\calt_enforcer_cmd", timeoutMs)) {
        return false;
      }
      pipe = CreateFileW(L"\\\\.\\pipe\\calt_enforcer_cmd", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, 0, nullptr);
    }
    if (pipe == INVALID_HANDLE_VALUE) return false;
  }

  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

  DWORD written = 0;
  if (!WriteFile(pipe, jsonReq.data(), (DWORD)jsonReq.size(), &written, nullptr)) {
    CloseHandle(pipe);
    return false;
  }

  // plan.overlay payloads grow with session count; drain MESSAGE frames fully.
  std::string acc;
  char buf[256 * 1024];
  for (;;) {
    DWORD read = 0;
    SetLastError(0);
    const BOOL ok = ReadFile(pipe, buf, sizeof(buf) - 1, &read, nullptr);
    const DWORD err = GetLastError();
    if (read > 0) acc.append(buf, read);
    if (ok) break;
    if (err == ERROR_MORE_DATA) continue;
    CloseHandle(pipe);
    return false;
  }
  if (acc.empty()) {
    CloseHandle(pipe);
    return false;
  }
  jsonResp.swap(acc);
  CloseHandle(pipe);
  return true;
}

}  // namespace

bool EnforcerSendCommand(const std::string& jsonReq, std::string& jsonResp, unsigned timeoutMs) {
  // The enforcer serves pipe instances from its tick loop: connecting while it
  // recycles can miss once, so retry before giving up (a false failure here
  // surfaces as enforcer_unreachable in Focus).
  const unsigned perTry = timeoutMs < 400 ? timeoutMs : (timeoutMs / 4);
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (SendOnce(jsonReq, jsonResp, perTry > 200 ? perTry : 250)) return true;
    Sleep(40 + (unsigned)attempt * 30);
  }
  return false;
}
