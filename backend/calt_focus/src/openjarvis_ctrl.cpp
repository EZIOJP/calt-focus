#include "openjarvis_ctrl.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr unsigned short kOjPort = 8000;
constexpr wchar_t kDefaultModel[] = L"qwen3.5:2b";

std::wstring WidenUtf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string NarrowUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

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

std::string ReadUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

unsigned long PidFromServerJson() {
  wchar_t home[MAX_PATH]{};
  if (!GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH)) return 0;
  const std::wstring path = std::wstring(home) + L"\\.openjarvis\\server.json";
  const std::string body = ReadUtf8(path);
  size_t p = body.find("\"pid\"");
  if (p == std::string::npos) return 0;
  p = body.find(':', p);
  if (p == std::string::npos) return 0;
  return (unsigned long)atoi(body.c_str() + p + 1);
}

bool PortOpen(unsigned short port) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  u_long nonblock = 1;
  ioctlsocket(s, FIONBIO, &nonblock);
  const int r = connect(s, (sockaddr*)&addr, sizeof(addr));
  bool ok = false;
  if (r == 0) {
    ok = true;
  } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval tv{0, 250000};
    if (select(0, nullptr, &w, nullptr, &tv) > 0) ok = true;
  }
  closesocket(s);
  return ok;
}

bool HealthOk() {
  // Never use system proxy for loopback (same bug as llm_http_proxy).
  HINTERNET ses = WinHttpOpen(L"CALT-Focus-OJ/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return PortOpen(kOjPort);
  WinHttpSetTimeouts(ses, 2000, 2000, 2000, 2000);
  HINTERNET con = WinHttpConnect(ses, L"127.0.0.1", kOjPort, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return false;
  }
  HINTERNET req =
      WinHttpOpenRequest(con, L"GET", L"/health", nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                               0) &&
            WinHttpReceiveResponse(req, nullptr);
  DWORD status = 0;
  if (ok) {
    DWORD sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  return ok && status >= 200 && status < 300;
}

std::wstring FindJarvisExe() {
  wchar_t home[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH)) {
    const std::wstring a = std::wstring(home) + L"\\OpenJarvis\\.venv\\Scripts\\jarvis.exe";
    if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES) return a;
  }
  wchar_t local[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
    const std::wstring b = std::wstring(local) + L"\\OpenJarvis\\jarvis.exe";
    if (GetFileAttributesW(b.c_str()) != INVALID_FILE_ATTRIBUTES) return b;
  }
  // PATH lookup
  wchar_t found[MAX_PATH]{};
  if (SearchPathW(nullptr, L"jarvis.exe", nullptr, MAX_PATH, found, nullptr)) return found;
  return {};
}

std::wstring FindOllamaExe() {
  wchar_t local[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
    const std::wstring a = std::wstring(local) + L"\\Programs\\Ollama\\ollama.exe";
    if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES) return a;
  }
  wchar_t found[MAX_PATH]{};
  if (SearchPathW(nullptr, L"ollama.exe", nullptr, MAX_PATH, found, nullptr)) return found;
  return {};
}

bool RunDetached(const std::wstring& exe, const std::wstring& args, DWORD waitMs, std::string* err) {
  if (exe.empty()) {
    if (err) *err = "exe_missing";
    return false;
  }
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmd;
  const std::wstring cwd = exe.substr(0, exe.find_last_of(L"\\/"));
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
    if (err) *err = "create_process_failed";
    return false;
  }
  if (waitMs) WaitForSingleObject(pi.hProcess, waitMs);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool RunJarvis(const std::wstring& exe, const std::wstring& args, DWORD waitMs, std::string* err) {
  if (exe.empty()) {
    if (err) *err = "jarvis_exe_missing";
    return false;
  }
  return RunDetached(exe, args, waitMs, err);
}

/** Local OpenJarvis models need Ollama :11434 — kick serve if the port is closed. */
bool EnsureOllamaUp() {
  if (PortOpen(11434)) return true;
  const std::wstring exe = FindOllamaExe();
  if (exe.empty()) return false;
  std::string ign;
  if (!RunDetached(exe, L"serve", 0, &ign)) return false;
  for (int i = 0; i < 25; ++i) {
    if (PortOpen(11434)) return true;
    Sleep(400);
  }
  return PortOpen(11434);
}

std::wstring SanitizeModel(const std::string& model) {
  std::wstring w = WidenUtf8(model);
  std::wstring out;
  for (wchar_t c : w) {
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
        c == L'.' || c == L':' || c == L'-' || c == L'_' || c == L'/')
      out.push_back(c);
  }
  if (out.empty()) out = kDefaultModel;
  return out;
}

}  // namespace

OpenJarvisHostStatus OpenJarvisHostProbe() {
  OpenJarvisHostStatus st;
  st.jarvis_exe = NarrowUtf8(FindJarvisExe());
  st.pid = PidFromServerJson();
  st.reachable = HealthOk();
  return st;
}

bool OpenJarvisHostEnsure(const std::string& model, std::string* err) {
  if (HealthOk()) return true;
  const std::wstring exe = FindJarvisExe();
  if (exe.empty()) {
    if (err) *err = "jarvis_exe_missing — install OpenJarvis CLI (USERPROFILE\\OpenJarvis\\.venv)";
    return false;
  }
  // Stale ~/.openjarvis/server.json after a crash leaves Health OFF until restart.
  if (!EnsureOllamaUp()) {
    if (err) *err = "ollama_down — start Ollama, then retry Auto-start API";
    // Still try jarvis start; some installs may not need :11434 for cloud engines.
  }
  const std::wstring m = SanitizeModel(model);
  // Background daemon — no GUI. Matches OpenJarvis `jarvis start`.
  std::wstring args = L"start --host 127.0.0.1 --port 8000 -a simple -m ";
  args += m;
  std::string startErr;
  if (!RunJarvis(exe, args, 12000, &startErr)) {
    if (err) *err = startErr.empty() ? "start_failed" : startErr;
    return false;
  }
  // Serve can take a while on first model warm.
  for (int i = 0; i < 45; ++i) {
    if (HealthOk()) return true;
    Sleep(1000);
  }
  if (err) {
    if (!PortOpen(11434))
      *err = "started_but_health_timeout (Ollama :11434 still down)";
    else
      *err = "started_but_health_timeout";
  }
  return HealthOk();
}

bool OpenJarvisHostStop(std::string* err) {
  const std::wstring exe = FindJarvisExe();
  if (exe.empty()) {
    if (err) *err = "jarvis_exe_missing";
    return false;
  }
  std::string stopErr;
  if (!RunJarvis(exe, L"stop", 8000, &stopErr)) {
    if (err) *err = stopErr.empty() ? "stop_failed" : stopErr;
    return false;
  }
  Sleep(400);
  return !HealthOk();
}

std::string OpenJarvisHostStatusJson(const OpenJarvisHostStatus& st) {
  return std::string("{\"reachable\":") + (st.reachable ? "true" : "false") +
         ",\"pid\":" + std::to_string((unsigned long long)st.pid) + ",\"jarvis_exe\":\"" +
         EscapeJson(st.jarvis_exe) + "\",\"model\":\"" + EscapeJson(st.model) + "\"" +
         (st.error.empty() ? "" : (",\"error\":\"" + EscapeJson(st.error) + "\"")) + "}";
}
