#include "sync_host_ctrl.h"
#include "paths.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::wstring SyncHostExe() {
  std::vector<std::wstring> roots{RepoRoot()};

  // An installed shell can have a different exe directory than the active
  // data plane. Its behavior directory is authoritative, and normally lives
  // at <repo>\\data\\productivity\\behavior.
  std::wstring fromBehavior = DataBehaviorDir();
  for (int i = 0; i < 3; ++i) {
    const size_t slash = fromBehavior.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
      fromBehavior.clear();
      break;
    }
    fromBehavior.resize(slash);
  }
  if (!fromBehavior.empty() && fromBehavior != roots.front()) roots.push_back(fromBehavior);

  for (const auto& root : roots) {
    const std::wstring a = root + L"\\backend\\calt_sync_host\\build\\calt_sync_host.exe";
    if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES) return a;
    const std::wstring b = root + L"\\backend\\calt_sync_host\\build\\Release\\calt_sync_host.exe";
    if (GetFileAttributesW(b.c_str()) != INVALID_FILE_ATTRIBUTES) return b;
  }
  return roots.front() + L"\\backend\\calt_sync_host\\build\\calt_sync_host.exe";
}

std::string ReadUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string JsonStringNear(const std::string& body, const char* key) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return {};
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return {};
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n'))
    ++i;
  if (i >= body.size() || body[i] != '"') return {};
  ++i;
  std::string tok;
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
      tok.push_back(body[i + 1]);
      i += 2;
      continue;
    }
    tok.push_back(body[i++]);
  }
  return tok;
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
    timeval tv{0, 200000};
    if (select(0, nullptr, &w, nullptr, &tv) > 0) ok = true;
  }
  closesocket(s);
  return ok;
}

DWORD FindSyncHostPid() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  DWORD found = 0;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"calt_sync_host.exe") == 0) {
        found = pe.th32ProcessID;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return found;
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

}  // namespace

SyncHostStatus SyncHostProbe() {
  SyncHostStatus st;
  st.pid = FindSyncHostPid();
  st.running = st.pid != 0;
  st.reachable = PortOpen(8765);
  const std::string mirror = ReadUtf8(DataBehaviorDir() + L"\\calt_sync_url.json");
  st.base_url = JsonStringNear(mirror, "base_url");
  st.token = JsonStringNear(mirror, "token");
  if (st.base_url.empty()) st.base_url = "http://192.168.0.110:8765";
  if (st.token.empty()) st.token = "calt-local-wearables";
  return st;
}

bool SyncHostStart(std::string* err) {
  SyncHostStatus cur = SyncHostProbe();
  if (cur.running && cur.reachable) return true;

  const std::wstring root = RepoRoot();
  SetEnvironmentVariableW(L"CALT_REPO", root.c_str());

  // Fast path: start the receiver first so the Health toggle replies within the FE
  // timeout. Ensure script (firewall + LAN URL mirror) runs in the background.
  const std::wstring exe = SyncHostExe();
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (err) *err = "missing_calt_sync_host_exe — build scripts\\build\\build_calt_sync_host.bat";
    return false;
  }
  if (!FindSyncHostPid()) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        root.c_str(), &si, &pi)) {
      if (err) *err = "create_process_failed";
      return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }

  // Kick ensure script without blocking the WebView message thread for long.
  const std::wstring ensure = root + L"\\scripts\\run\\ensure_calt_sync_host.ps1";
  if (GetFileAttributesW(ensure.c_str()) != INVALID_FILE_ATTRIBUTES) {
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"";
    cmd += ensure;
    cmd += L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, root.c_str(), &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);  // do not wait — firewall/LAN probe can take >15s
    }
  }

  for (int i = 0; i < 20; ++i) {
    Sleep(150);
    SyncHostStatus after = SyncHostProbe();
    if (after.running || after.reachable) return true;
  }
  if (err) *err = "started_but_unreachable";
  return false;
}

bool SyncHostStop(std::string* err) {
  DWORD pid = FindSyncHostPid();
  if (!pid) return true;
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (!h) {
    if (err) *err = "open_process_failed";
    return false;
  }
  const BOOL ok = TerminateProcess(h, 0);
  CloseHandle(h);
  if (!ok) {
    if (err) *err = "terminate_failed";
    return false;
  }
  Sleep(200);
  return true;
}

std::string SyncHostStatusJson(const SyncHostStatus& st) {
  // Fields only (no outer ok) — Focus merges into sync_host_result.
  return std::string("{\"running\":") + (st.running ? "true" : "false") +
         ",\"reachable\":" + (st.reachable ? "true" : "false") + ",\"pid\":" +
         std::to_string((unsigned long long)st.pid) + ",\"base_url\":\"" + EscapeJson(st.base_url) +
         "\",\"token\":\"" + EscapeJson(st.token) + "\"" +
         (st.error.empty() ? "" : (",\"error\":\"" + EscapeJson(st.error) + "\"")) + "}";
}
