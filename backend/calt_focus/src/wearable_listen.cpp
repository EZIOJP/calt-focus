#include <winsock2.h>
#include <ws2tcpip.h>

#include "wearable_listen.h"

#include "assistant_host.h"
#include "enforcer_cmd.h"
#include "local_brain.h"
#include "speech_host.h"
#include "nutrition_host.h"
#include "paths.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr int kMaxBody = 10 * 1024 * 1024;

SOCKET gListen = INVALID_SOCKET;
HANDLE gThread = nullptr;
volatile LONG gStop = 0;
std::wstring gBehavior;
int gPort = 8765;

bool SendAll(SOCKET s, const char* data, int n) {
  int off = 0;
  while (off < n) {
    int w = send(s, data + off, n - off, 0);
    if (w <= 0) return false;
    off += w;
  }
  return true;
}

void SendRaw(SOCKET s, int code, const std::string& body, bool json) {
  const char* reason = "OK";
  if (code == 204) reason = "No Content";
  else if (code == 400) reason = "Bad Request";
  else if (code == 401) reason = "Unauthorized";
  else if (code == 404) reason = "Not Found";
  else if (code == 411) reason = "Length Required";
  else if (code == 413) reason = "Payload Too Large";
  else if (code == 503) reason = "Service Unavailable";
  else if (code != 200) reason = "Error";
  std::string hdr = "HTTP/1.1 " + std::to_string(code) + " " + reason +
                    "\r\nAccess-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Headers: Authorization, Content-Type, X-CALT-Wearable-Key\r\n"
                    "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                    "Connection: close\r\n";
  if (json) {
    hdr += "Content-Type: application/json; charset=utf-8\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n";
    hdr += body;
  } else {
    hdr += "Content-Length: 0\r\n\r\n";
  }
  SendAll(s, hdr.data(), (int)hdr.size());
}

void SendJson(SOCKET s, int code, const std::string& body) { SendRaw(s, code, body, true); }

void SendTyped(SOCKET s, int code, const char* contentType, const std::string& body) {
  const char* reason = code == 200 ? "OK" : "Not Found";
  std::string hdr = std::string("HTTP/1.1 ") + std::to_string(code) + " " + reason +
                    "\r\nContent-Type: " + contentType +
                    "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nAccess-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Headers: Authorization, Content-Type, X-CALT-Wearable-Key\r\n"
                    "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                    "Cache-Control: no-cache\r\nConnection: close\r\n\r\n";
  hdr += body;
  SendAll(s, hdr.data(), (int)hdr.size());
}

bool ReadFileMax(const std::wstring& path, std::string* out, size_t maxBytes) {
  out->clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || (unsigned long long)sz.QuadPart > maxBytes) {
    CloseHandle(h);
    return false;
  }
  out->resize((size_t)sz.QuadPart);
  DWORD read = 0;
  BOOL ok = ReadFile(h, out->data(), (DWORD)out->size(), &read, nullptr);
  CloseHandle(h);
  return ok && read == out->size();
}

bool ServeNutriPage(SOCKET s, const std::string& path) {
  const std::wstring root = RepoRoot() + L"\\scripts\\run\\static_mobile";
  std::wstring file;
  const char* type = "text/html; charset=utf-8";
  if (path == "/n" || path == "/nutri" || path == "/nutrition/app" || path == "/mobile") {
    file = root + L"\\nutri.html";
  } else if (path == "/assistant" || path == "/coach") {
    file = root + L"\\assistant.html";
  } else if (path == "/n/manifest.webmanifest") {
    file = root + L"\\manifest.webmanifest";
    type = "application/manifest+json";
  } else if (path == "/n/icon.svg") {
    file = root + L"\\icon.svg";
    type = "image/svg+xml";
  } else {
    return false;
  }
  std::string body;
  if (!ReadFileMax(file, &body, 2 * 1024 * 1024)) {
    SendJson(s, 404, "{\"ok\":false,\"detail\":\"page missing\"}");
    return true;
  }
  SendTyped(s, 200, type, body);
  return true;
}

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string HeaderValue(const std::string& headers, const char* name) {
  const std::string want = Lower(name);
  size_t i = 0;
  while (i < headers.size()) {
    size_t end = headers.find("\r\n", i);
    if (end == std::string::npos) end = headers.size();
    std::string line = headers.substr(i, end - i);
    i = (end == headers.size()) ? headers.size() : end + 2;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = Lower(line.substr(0, colon));
    if (key != want) continue;
    size_t v = colon + 1;
    while (v < line.size() && line[v] == ' ') ++v;
    std::string val = line.substr(v);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\r' || val.back() == '\t'))
      val.pop_back();
    return val;
  }
  return "";
}

std::string ExpectedKey() {
  if (const char* e = std::getenv("WEARABLES_INGEST_KEY")) {
    if (e[0]) return e;
  }
  if (const char* e = std::getenv("CALT_WEARABLES_KEY")) {
    if (e[0]) return e;
  }
  return "calt-local-wearables";
}

bool AuthOk(const std::string& headers) {
  const std::string expect = ExpectedKey();
  std::string provided = HeaderValue(headers, "x-calt-wearable-key");
  if (provided.empty()) {
    std::string auth = HeaderValue(headers, "authorization");
    const std::string low = Lower(auth);
    const std::string pfx = "bearer ";
    if (low.size() > pfx.size() && low.compare(0, pfx.size(), pfx) == 0) {
      provided = auth.substr(pfx.size());
      while (!provided.empty() && provided.front() == ' ') provided.erase(provided.begin());
    }
  }
  if (provided.size() != expect.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < expect.size(); ++i) {
    diff |= (unsigned char)provided[i] ^ (unsigned char)expect[i];
  }
  return diff == 0;
}

bool RecvSome(SOCKET s, std::string* buf, size_t max) {
  while (buf->find("\r\n\r\n") == std::string::npos) {
    if (buf->size() >= max) return false;
    char tmp[4096];
    int n = recv(s, tmp, (int)sizeof(tmp), 0);
    if (n <= 0) return false;
    buf->append(tmp, tmp + n);
  }
  return true;
}

bool RecvBody(SOCKET s, std::string* buf, size_t headerEnd, size_t contentLength) {
  size_t have = buf->size() - (headerEnd + 4);
  while (have < contentLength) {
    if (buf->size() > headerEnd + 4 + (size_t)kMaxBody) return false;
    char tmp[8192];
    int n = recv(s, tmp, (int)sizeof(tmp), 0);
    if (n <= 0) return false;
    buf->append(tmp, tmp + n);
    have += (size_t)n;
  }
  return true;
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

bool WriteSpool(const std::wstring& path, const std::string& body) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = WriteFile(h, body.data(), (DWORD)body.size(), &written, nullptr);
  CloseHandle(h);
  if (!ok || written != body.size()) {
    DeleteFileW(path.c_str());
    return false;
  }
  return true;
}

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return "";
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  for (char& c : s) {
    if (c == '\\') c = '/';
  }
  return s;
}

bool ReadText(const std::wstring& path, std::string* out) {
  out->clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  char tmp[8192];
  DWORD read = 0;
  while (ReadFile(h, tmp, sizeof(tmp), &read, nullptr) && read > 0) {
    out->append(tmp, tmp + read);
    if (out->size() > 256 * 1024) break;
  }
  CloseHandle(h);
  return true;
}

/** Pull a JSON string or null. Returns false if the key is absent. */
bool PullJsonString(const std::string& body, const char* key, std::string* out, bool* isNull) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (body.compare(i, 4, "null") == 0) {
    *isNull = true;
    out->clear();
    return true;
  }
  if (i >= body.size() || body[i] != '"') return false;
  ++i;
  out->clear();
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
      out->push_back(body[i + 1]);
      i += 2;
      continue;
    }
    out->push_back(body[i++]);
  }
  *isNull = false;
  return true;
}

bool PullBool(const std::string& body, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (body.compare(i, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (body.compare(i, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

std::string JsonQuote(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(c);
    } else {
      o.push_back(c);
    }
  }
  o.push_back('"');
  return o;
}

std::string HealthJson(bool enforcerOk, const std::string& received, bool haveReceived, bool watch) {
  std::string at = haveReceived ? JsonQuote(received) : "null";
  return std::string("{\"ok\":true,\"service\":\"calt.focus.wearables\",\"receiver\":\"calt_focus\",") +
         "\"store\":\"calt_enforcer\",\"port\":" + std::to_string(gPort) +
         ",\"enforcer_ok\":" + (enforcerOk ? "true" : "false") + ",\"last_received_at\":" + at +
         ",\"received\":" + (haveReceived ? "true" : "false") +
         ",\"watch_received\":" + (watch ? "true" : "false") +
         ",\"nutri\":\"/n\",\"nutrition\":\"/api/nutrition\",\"brain_ready\":" +
         (LocalBrainStatus().ready ? "true" : "false") +
         ",\"speech_ready\":" + (SpeechStatus().ready ? "true" : "false") + "}";
}

void LoadReceipt(std::string* received, bool* have, bool* watch, bool* enforcerOk) {
  *have = false;
  *watch = false;
  *enforcerOk = false;
  received->clear();
  std::string resp;
  const std::string req = "{\"op\":\"wearable.status\",\"id\":\"wh\",\"v\":1,\"payload\":{}}";
  if (EnforcerSendCommand(req, resp, 4000)) {
    bool ok = false;
    if (PullBool(resp, "ok", &ok) && ok) *enforcerOk = true;
    bool isNull = false;
    if (PullJsonString(resp, "last_received_at", received, &isNull) && !isNull && !received->empty()) {
      *have = true;
    }
    bool w = false;
    if (PullBool(resp, "watch_received", &w)) *watch = w;
    if (*enforcerOk) return;
  }
  std::string file;
  if (!ReadText(Join(gBehavior, L"wearables_last_sync.json"), &file)) return;
  bool isNull = false;
  std::string at;
  if (PullJsonString(file, "last_received_at", &at, &isNull) && !isNull && !at.empty()) {
    *received = at;
    *have = true;
  }
  bool w = false;
  if (PullBool(file, "last_is_watch", &w) || PullBool(file, "watch_received", &w)) *watch = w && *have;
}

std::string LocalToday() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
  return buf;
}

bool IsYmd(const std::string& s) {
  if (s.size() != 10) return false;
  for (int i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) {
      if (s[i] != '-') return false;
    } else if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }
  return true;
}

int ListenPort() {
  const char* e = std::getenv("CALT_WEARABLES_PORT");
  if (!e || !e[0]) e = std::getenv("TRACKER_HUB_PORT");
  if (!e || !e[0]) return 8765;
  int p = std::atoi(e);
  if (p < 1 || p > 65535) return 8765;
  return p;
}

std::string NormalizePath(std::string path) {
  size_t q = path.find('?');
  if (q != std::string::npos) path.resize(q);
  if (path.size() > 1 && path.back() == '/') path.pop_back();
  return path;
}

void HandleClient(SOCKET s) {
  DWORD ms = 60000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&ms, sizeof(ms));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&ms, sizeof(ms));
  std::string buf;
  if (!RecvSome(s, &buf, 64 * 1024)) {
    SendJson(s, 400, "{\"ok\":false,\"error\":\"bad_request\"}");
    return;
  }
  size_t hdrEnd = buf.find("\r\n\r\n");
  std::string head = buf.substr(0, hdrEnd);
  size_t lineEnd = head.find("\r\n");
  std::string reqLine = lineEnd == std::string::npos ? head : head.substr(0, lineEnd);
  size_t sp1 = reqLine.find(' ');
  size_t sp2 = sp1 == std::string::npos ? std::string::npos : reqLine.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    SendJson(s, 400, "{\"ok\":false,\"error\":\"bad_request\"}");
    return;
  }
  std::string method = reqLine.substr(0, sp1);
  std::string path = NormalizePath(reqLine.substr(sp1 + 1, sp2 - sp1 - 1));

  if (method == "OPTIONS") {
    SendRaw(s, 204, "", false);
    return;
  }

  if (method == "GET" && ServeNutriPage(s, path)) return;

  if (path == "/api/assistant" || path.rfind("/api/assistant/", 0) == 0) {
    std::string body;
    if (method == "POST") {
      std::string lenText = HeaderValue(head, "content-length");
      if (lenText.empty()) {
        SendJson(s, 411, "{\"ok\":false,\"error\":\"length_required\"}");
        return;
      }
      long long len = std::atoll(lenText.c_str());
      if (len < 2 || len > 200000) {
        SendJson(s, 413, "{\"ok\":false,\"error\":\"too_large\"}");
        return;
      }
      if (!RecvBody(s, &buf, hdrEnd, (size_t)len)) {
        SendJson(s, 400, "{\"ok\":false,\"error\":\"body\"}");
        return;
      }
      body = buf.substr(hdrEnd + 4, (size_t)len);
    }
    AssistantReply reply = AssistantHandle(gBehavior, method, path, body);
    SendJson(s, reply.code, reply.json);
    return;
  }

  if (path == "/api/speech" || path.rfind("/api/speech/", 0) == 0 || path == "/api/tts" ||
      path.rfind("/api/tts/", 0) == 0) {
    std::string body;
    if (method == "POST") {
      std::string lenText = HeaderValue(head, "content-length");
      if (lenText.empty()) {
        SendJson(s, 411, "{\"ok\":false,\"error\":\"length_required\"}");
        return;
      }
      long long len = std::atoll(lenText.c_str());
      if (len < 2 || len > 32000) {
        SendJson(s, 413, "{\"ok\":false,\"error\":\"too_large\"}");
        return;
      }
      if (!RecvBody(s, &buf, hdrEnd, (size_t)len)) {
        SendJson(s, 400, "{\"ok\":false,\"error\":\"body\"}");
        return;
      }
      body = buf.substr(hdrEnd + 4, (size_t)len);
    }
    SpeechReply reply = SpeechHandle(method, path, body);
    if (reply.contentType.find("json") != std::string::npos) SendJson(s, reply.code, reply.body);
    else SendTyped(s, reply.code, reply.contentType.c_str(), reply.body);
    return;
  }

  if (path == "/api/brain" || path.rfind("/api/brain/", 0) == 0) {
    std::string body;
    if (method == "POST") {
      std::string lenText = HeaderValue(head, "content-length");
      if (lenText.empty()) {
        SendJson(s, 411, "{\"ok\":false,\"error\":\"length_required\"}");
        return;
      }
      long long len = std::atoll(lenText.c_str());
      if (len < 2 || len > 200000) {
        SendJson(s, 413, "{\"ok\":false,\"error\":\"too_large\"}");
        return;
      }
      if (!RecvBody(s, &buf, hdrEnd, (size_t)len)) {
        SendJson(s, 400, "{\"ok\":false,\"error\":\"body\"}");
        return;
      }
      body = buf.substr(hdrEnd + 4, (size_t)len);
    }
    BrainReply reply = LocalBrainHandle(method, path, body);
    SendJson(s, reply.code, reply.json);
    return;
  }

  if (path == "/api/nutrition" || path.rfind("/api/nutrition/", 0) == 0) {
    std::string body;
    if (method == "POST") {
      std::string lenText = HeaderValue(head, "content-length");
      if (lenText.empty()) {
        SendJson(s, 411, "{\"ok\":false,\"error\":\"length_required\"}");
        return;
      }
      long long len = std::atoll(lenText.c_str());
      const long long cap = path == "/api/nutrition/analyze-photo" ? kMaxBody : 200000;
      if (len < 2 || len > cap) {
        SendJson(s, 413, "{\"ok\":false,\"error\":\"too_large\"}");
        return;
      }
      if (!RecvBody(s, &buf, hdrEnd, (size_t)len)) {
        SendJson(s, 400, "{\"ok\":false,\"error\":\"body\"}");
        return;
      }
      body = buf.substr(hdrEnd + 4, (size_t)len);
    }
    NutriReply reply = NutriHandle(gBehavior, method, path, body);
    SendJson(s, reply.code, reply.json);
    return;
  }

  const bool wearRoute = path == "/api/wearables/zepp" || path.rfind("/api/wearables/zepp/", 0) == 0;
  if (wearRoute && !AuthOk(head)) {
    SendJson(s, 401, "{\"ok\":false,\"detail\":\"Invalid wearable ingest key\"}");
    return;
  }

  if (method == "GET" && (path == "/health" || path == "/api/wearables/zepp/health" ||
                          path == "/api/wearables/zepp/status")) {
    std::string received;
    bool have = false, watch = false, enforcerOk = false;
    LoadReceipt(&received, &have, &watch, &enforcerOk);
    SendJson(s, 200, HealthJson(enforcerOk, received, have, watch));
    return;
  }

  if (method == "POST" && path == "/api/wearables/zepp") {
    std::string lenText = HeaderValue(head, "content-length");
    if (lenText.empty()) {
      SendJson(s, 411, "{\"ok\":false,\"error\":\"length_required\"}");
      return;
    }
    long long len = std::atoll(lenText.c_str());
    if (len < 2 || len > kMaxBody) {
      SendJson(s, 413, "{\"ok\":false,\"error\":\"too_large\"}");
      return;
    }
    if (!RecvBody(s, &buf, hdrEnd, (size_t)len)) {
      SendJson(s, 400, "{\"ok\":false,\"error\":\"body\"}");
      return;
    }
    std::string body = buf.substr(hdrEnd + 4, (size_t)len);
    std::wstring inbox = Join(gBehavior, L"wearable_inbox");
    CreateDirectoryW(gBehavior.c_str(), nullptr);
    CreateDirectoryW(inbox.c_str(), nullptr);
    static volatile LONG seq = 0;
    LONG n = InterlockedIncrement(&seq);
    std::wstring file = Join(inbox, L"in_" + std::to_wstring(GetTickCount64()) + L"_" +
                                         std::to_wstring(n) + L".json");
    if (!WriteSpool(file, body)) {
      SendJson(s, 500, "{\"ok\":false,\"error\":\"spool_failed\"}");
      return;
    }
    std::string utf = WideToUtf8(file);
    std::string cmd = std::string("{\"op\":\"wearable.ingest\",\"id\":\"w") + std::to_string(n) +
                      "\",\"v\":1,\"payload\":{\"spool\":\"" + utf + "\"}}";
    std::string resp;
    bool sent = EnforcerSendCommand(cmd, resp, 8000);
    DeleteFileW(file.c_str());
    if (!sent) {
      SendJson(s, 503, "{\"ok\":false,\"error\":\"enforcer_unavailable\",\"stored_by\":\"calt_enforcer\"}");
      return;
    }
    bool ok = false;
    int code = (PullBool(resp, "ok", &ok) && ok) ? 200 : 400;
    SendJson(s, code, resp);
    return;
  }

  // Health screen reads the same daily row the Python hub used to serve.
  if (method == "GET" && (path == "/api/life/daily" || path.rfind("/api/life/daily/", 0) == 0)) {
    std::string day = "today";
    const std::string prefix = "/api/life/daily/";
    if (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0) {
      day = path.substr(prefix.size());
    }
    if (day == "today" || day.empty()) day = LocalToday();
    if (!IsYmd(day)) {
      SendJson(s, 400, "{\"ok\":false,\"error\":\"bad_date\"}");
      return;
    }
    std::wstring wday(day.begin(), day.end());
    std::string body;
    ReadText(Join(Join(gBehavior, L"life_days"), wday + L".json"), &body);
    auto looksJson = [](const std::string& raw) {
      for (char c : raw) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        return c == '{';
      }
      return false;
    };
    if (!looksJson(body) && day == LocalToday()) {
      body.clear();
      ReadText(Join(gBehavior, L"life_today.json"), &body);
    }
    if (!looksJson(body)) {
      body = std::string("{\"date\":\"") + day + "\",\"empty\":true}";
    }
    SendJson(s, 200, body);
    return;
  }

  SendJson(s, 404, "{\"ok\":false,\"detail\":\"unknown wearables route\"}");
}

DWORD WINAPI ListenThread(LPVOID) {
  while (InterlockedCompareExchange(&gStop, 0, 0) == 0) {
    SOCKET client = accept(gListen, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
      if (InterlockedCompareExchange(&gStop, 0, 0) != 0) break;
      if (WSAGetLastError() == WSAEINTR) break;
      continue;
    }
    HandleClient(client);
    closesocket(client);
  }
  return 0;
}

}  // namespace

bool WearableListenStart(const std::wstring& behaviorDir) {
  if (gThread) return true;
  gBehavior = behaviorDir;
  gPort = ListenPort();
  gStop = 0;
  gListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (gListen == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)gPort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(gListen, (sockaddr*)&addr, sizeof(addr)) != 0) {
    closesocket(gListen);
    gListen = INVALID_SOCKET;
    return false;
  }
  if (listen(gListen, 8) != 0) {
    closesocket(gListen);
    gListen = INVALID_SOCKET;
    return false;
  }
  gThread = CreateThread(nullptr, 0, ListenThread, nullptr, 0, nullptr);
  if (!gThread) {
    closesocket(gListen);
    gListen = INVALID_SOCKET;
    return false;
  }
  return true;
}

void WearableListenStop() {
  InterlockedExchange(&gStop, 1);
  if (gListen != INVALID_SOCKET) {
    closesocket(gListen);
    gListen = INVALID_SOCKET;
  }
  if (gThread) {
    WaitForSingleObject(gThread, 2000);
    CloseHandle(gThread);
    gThread = nullptr;
  }
}
