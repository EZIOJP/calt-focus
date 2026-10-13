#include "enforcer_cmd.h"

#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\calt_enforcer_cmd";

bool WaitOverlapped(HANDLE pipe, OVERLAPPED& ov, DWORD timeoutMs, DWORD* transferred) {
  *transferred = 0;
  const DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
  if (wait == WAIT_OBJECT_0) {
    return GetOverlappedResult(pipe, &ov, transferred, FALSE) != FALSE;
  }
  CancelIoEx(pipe, &ov);
  WaitForSingleObject(ov.hEvent, 2000);
  DWORD ignored = 0;
  GetOverlappedResult(pipe, &ov, &ignored, FALSE);
  return false;
}

/** Sync-style request/response with a hard deadline (no shared-event races). */
bool SendOnce(const std::string& jsonReq, std::string& jsonResp, unsigned timeoutMs) {
  jsonResp.clear();
  if (timeoutMs < 200) timeoutMs = 200;

  // Prefer a free listener; FILE_FLAG_OVERLAPPED only for deadline-bounded I/O.
  HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    const DWORD err = GetLastError();
    if (err == ERROR_PIPE_BUSY || err == ERROR_FILE_NOT_FOUND) {
      const DWORD waitPipe = timeoutMs > 2500 ? 2500 : timeoutMs;
      if (!WaitNamedPipeW(kPipeName, waitPipe)) return false;
      pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (pipe == INVALID_HANDLE_VALUE) return false;
  }

  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

  // Separate events — reusing one event for write+read dropped completions.
  HANDLE evWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE evRead = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!evWrite || !evRead) {
    if (evWrite) CloseHandle(evWrite);
    if (evRead) CloseHandle(evRead);
    CloseHandle(pipe);
    return false;
  }

  const DWORD deadline = GetTickCount() + timeoutMs;
  auto leftMs = [&]() -> DWORD {
    const DWORD now = GetTickCount();
    return now >= deadline ? 0 : (deadline - now);
  };

  OVERLAPPED ovWrite{};
  ovWrite.hEvent = evWrite;
  DWORD written = 0;
  BOOL wOk = WriteFile(pipe, jsonReq.data(), (DWORD)jsonReq.size(), &written, &ovWrite);
  if (!wOk) {
    if (GetLastError() != ERROR_IO_PENDING || !WaitOverlapped(pipe, ovWrite, leftMs(), &written)) {
      CloseHandle(evWrite);
      CloseHandle(evRead);
      CloseHandle(pipe);
      return false;
    }
  }

  std::string acc;
  char buf[256 * 1024];
  for (;;) {
    const DWORD left = leftMs();
    if (left == 0) {
      CloseHandle(evWrite);
      CloseHandle(evRead);
      CloseHandle(pipe);
      return false;
    }

    OVERLAPPED ovRead{};
    ovRead.hEvent = evRead;
    ResetEvent(evRead);
    DWORD read = 0;
    SetLastError(0);
    BOOL ok = ReadFile(pipe, buf, sizeof(buf) - 1, &read, &ovRead);
    DWORD err = GetLastError();
    if (!ok) {
      if (err == ERROR_IO_PENDING) {
        if (!WaitOverlapped(pipe, ovRead, left, &read)) {
          CloseHandle(evWrite);
          CloseHandle(evRead);
          CloseHandle(pipe);
          return false;
        }
        ok = TRUE;
        err = 0;
      } else if (err == ERROR_MORE_DATA) {
        // Partial message already in `read` — keep looping.
      } else {
        CloseHandle(evWrite);
        CloseHandle(evRead);
        CloseHandle(pipe);
        return false;
      }
    }

    if (read > 0) acc.append(buf, read);
    if (ok && err != ERROR_MORE_DATA) break;
  }

  CloseHandle(evWrite);
  CloseHandle(evRead);
  CloseHandle(pipe);
  if (acc.empty()) return false;
  jsonResp.swap(acc);
  return true;
}

void AgentDbg(const char* msg, const char* op, unsigned timeoutMs, bool ok, DWORD err) {
  // #region agent log
  wchar_t path[MAX_PATH];
  if (!GetEnvironmentVariableW(L"CALT_FOCUS_DEBUG_LOG", path, MAX_PATH)) {
    // Repo-relative fallback via known layout next to exe is fragile; write beside
    // the Focus data behavior dir when possible.
    const wchar_t* fallback = L"C:\\Users\\Lenovo\\Desktop\\calt-focus\\.cursor\\debug-56e50d.log";
    wcsncpy(path, fallback, MAX_PATH - 1);
    path[MAX_PATH - 1] = 0;
  }
  HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  char line[512];
  const int n = snprintf(
      line, sizeof(line),
      "{\"sessionId\":\"56e50d\",\"runId\":\"post-fix\",\"hypothesisId\":\"H\","
      "\"location\":\"enforcer_cmd.cpp\",\"message\":\"%s\",\"data\":{\"op\":\"%s\","
      "\"timeoutMs\":%u,\"ok\":%s,\"winerr\":%lu},\"timestamp\":%llu}\n",
      msg, op ? op : "", timeoutMs, ok ? "true" : "false", (unsigned long)err,
      (unsigned long long)GetTickCount64());
  if (n > 0) {
    DWORD w = 0;
    WriteFile(h, line, (DWORD)n, &w, nullptr);
  }
  CloseHandle(h);
  // #endregion
}

std::string ExtractOp(const std::string& jsonReq) {
  const char* key = "\"op\"";
  size_t p = jsonReq.find(key);
  if (p == std::string::npos) return {};
  size_t q1 = jsonReq.find('"', p + 4);
  if (q1 == std::string::npos) return {};
  size_t q2 = jsonReq.find('"', q1 + 1);
  if (q2 == std::string::npos || q2 <= q1 + 1) return {};
  // find value after :"
  size_t colon = jsonReq.find(':', p);
  if (colon == std::string::npos) return {};
  size_t v1 = jsonReq.find('"', colon + 1);
  if (v1 == std::string::npos) return {};
  size_t v2 = jsonReq.find('"', v1 + 1);
  if (v2 == std::string::npos) return {};
  return jsonReq.substr(v1 + 1, v2 - v1 - 1);
}

}  // namespace

bool EnforcerSendCommand(const std::string& jsonReq, std::string& jsonResp, unsigned timeoutMs) {
  // Bounded retries — never exceed timeoutMs wall time.
  if (timeoutMs < 400) timeoutMs = 400;
  const DWORD deadline = GetTickCount() + timeoutMs;
  // Prefer one longer try over three short ones that each burn the FE timer.
  const unsigned perTry = timeoutMs < 1200 ? timeoutMs : (timeoutMs * 2 / 3);
  const std::string op = ExtractOp(jsonReq);
  for (int attempt = 0; attempt < 2; ++attempt) {
    const DWORD now = GetTickCount();
    if (now >= deadline) break;
    const unsigned left = deadline - now;
    const unsigned slice = left < perTry ? left : perTry;
    if (SendOnce(jsonReq, jsonResp, slice > 150 ? slice : 150)) {
      AgentDbg("send_ok", op.c_str(), timeoutMs, true, 0);
      return true;
    }
    AgentDbg("send_fail", op.c_str(), slice, false, GetLastError());
    if (GetTickCount() >= deadline) break;
    Sleep(20 + (unsigned)attempt * 30);
  }
  AgentDbg("send_giveup", op.c_str(), timeoutMs, false, GetLastError());
  return false;
}
