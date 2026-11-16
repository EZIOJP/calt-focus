#include "llm_http_proxy.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace {

constexpr size_t kMaxReqBytes = 512 * 1024;
constexpr size_t kMaxResBytes = 16 * 1024 * 1024;

std::string Base64Encode(const std::string& in) {
  static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(kTable[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(kTable[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

std::wstring WidenUtf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string ToLower(std::string s) {
  for (char& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

bool ParseHttpUrl(const std::string& url, bool* https, std::wstring* host, INTERNET_PORT* port,
                  std::wstring* path) {
  std::string u = url;
  bool secure = false;
  size_t scheme = 0;
  if (u.rfind("https://", 0) == 0) {
    secure = true;
    scheme = 8;
  } else if (u.rfind("http://", 0) == 0) {
    scheme = 7;
  } else {
    return false;
  }
  size_t slash = u.find('/', scheme);
  std::string hostPort = slash == std::string::npos ? u.substr(scheme) : u.substr(scheme, slash - scheme);
  std::string pathStr = slash == std::string::npos ? "/" : u.substr(slash);
  if (pathStr.empty()) pathStr = "/";

  std::string hostA;
  INTERNET_PORT p = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
  size_t colon = hostPort.find(':');
  if (colon == std::string::npos) {
    hostA = hostPort;
  } else {
    hostA = hostPort.substr(0, colon);
    int pi = atoi(hostPort.c_str() + colon + 1);
    if (pi <= 0 || pi > 65535) return false;
    p = (INTERNET_PORT)pi;
  }
  if (hostA.empty()) return false;
  *https = secure;
  *host = WidenUtf8(hostA);
  *port = p;
  *path = WidenUtf8(pathStr);
  return !host->empty() && !path->empty();
}

}  // namespace

bool LlmHttpUrlAllowed(const std::string& url) {
  const std::string u = ToLower(url);
  auto starts = [&](const char* p) { return u.rfind(p, 0) == 0; };
  if (starts("http://127.0.0.1:8000/") || starts("http://127.0.0.1:8000?") || u == "http://127.0.0.1:8000")
    return true;
  if (starts("http://localhost:8000/") || starts("http://localhost:8000?") || u == "http://localhost:8000")
    return true;
  if (starts("http://127.0.0.1:11434/") || starts("http://127.0.0.1:11434?") ||
      u == "http://127.0.0.1:11434")
    return true;
  if (starts("http://localhost:11434/") || starts("http://localhost:11434?") ||
      u == "http://localhost:11434")
    return true;
  if (starts("https://openrouter.ai/")) return true;
  // NVIDIA NIM / NVCF TTS endpoints (magpie-tts-multilingual etc.)
  if (starts("https://") &&
      (u.find(".nvidia.com/") != std::string::npos ||
       u.find(".nvcf.nvidia.com/") != std::string::npos ||
       u.find("api.nvcf.nvidia.com/") != std::string::npos))
    return true;
  return false;
}

LlmHttpResult LlmHttpRequest(const std::string& method, const std::string& url,
                             const std::string& authorization, const std::string& contentType,
                             const std::string& body, unsigned timeoutMs, bool base64Response) {
  LlmHttpResult out;
  if (!LlmHttpUrlAllowed(url)) {
    out.error = "url_not_allowed";
    return out;
  }
  if (body.size() > kMaxReqBytes) {
    out.error = "request_too_large";
    return out;
  }
  std::string meth = method;
  for (char& c : meth) c = (char)toupper((unsigned char)c);
  if (meth != "GET" && meth != "POST" && meth != "HEAD") {
    out.error = "method_not_allowed";
    return out;
  }

  bool https = false;
  std::wstring host, path;
  INTERNET_PORT port = 0;
  if (!ParseHttpUrl(url, &https, &host, &port, &path)) {
    out.error = "bad_url";
    return out;
  }

  if (timeoutMs < 1000) timeoutMs = 1000;
  if (timeoutMs > 120000) timeoutMs = 120000;

  // Localhost must bypass system proxy — DEFAULT_PROXY often breaks 127.0.0.1
  // (Invoke-WebRequest works; WinHTTP returns ERROR_WINHTTP_CANNOT_CONNECT / send fail).
  const bool localHost =
      _wcsicmp(host.c_str(), L"127.0.0.1") == 0 || _wcsicmp(host.c_str(), L"localhost") == 0;
  HINTERNET ses =
      WinHttpOpen(L"CALT-Focus-LLM/1.0",
                  localHost ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    out.error = "winhttp_open";
    return out;
  }
  WinHttpSetTimeouts(ses, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs);

  HINTERNET con = WinHttpConnect(ses, host.c_str(), port, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    out.error = "winhttp_connect";
    return out;
  }

  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req = WinHttpOpenRequest(con, WidenUtf8(meth).c_str(), path.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    out.error = "winhttp_request";
    return out;
  }

  std::wstring hdr;
  if (!contentType.empty()) {
    hdr += L"Content-Type: ";
    hdr += WidenUtf8(contentType);
    hdr += L"\r\n";
  } else if (meth == "POST") {
    hdr += L"Content-Type: application/json\r\n";
  }
  if (!authorization.empty()) {
    hdr += L"Authorization: ";
    hdr += WidenUtf8(authorization);
    hdr += L"\r\n";
  }
  // OpenRouter attribution (harmless elsewhere)
  if (ToLower(url).find("openrouter.ai") != std::string::npos) {
    hdr += L"HTTP-Referer: https://calt.focus.local\r\n";
    hdr += L"X-Title: CALT Focus\r\n";
  }

  LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data();
  DWORD bodyLen = (DWORD)body.size();
  BOOL okSend = WinHttpSendRequest(req, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(),
                                   hdr.empty() ? 0 : (DWORD)-1L, bodyPtr, bodyLen, bodyLen, 0);
  if (!okSend) {
    const DWORD err = GetLastError();
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    char buf[48];
    snprintf(buf, sizeof(buf), "winhttp_send_%lu", (unsigned long)err);
    out.error = buf;
    return out;
  }
  if (!WinHttpReceiveResponse(req, nullptr)) {
    const DWORD err = GetLastError();
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    char buf[48];
    snprintf(buf, sizeof(buf), "winhttp_recv_%lu", (unsigned long)err);
    out.error = buf;
    return out;
  }

  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
  out.status = (int)status;

  out.body.clear();
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
    if (out.body.size() + avail > kMaxResBytes) {
      WinHttpCloseHandle(req);
      WinHttpCloseHandle(con);
      WinHttpCloseHandle(ses);
      out.error = "response_too_large";
      return out;
    }
    size_t old = out.body.size();
    out.body.resize(old + avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, out.body.data() + old, avail, &read)) break;
    out.body.resize(old + read);
  }

  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  if (base64Response) {
    out.body = Base64Encode(out.body);
    out.is_base64 = true;
  }
  out.ok = true;
  return out;
}
