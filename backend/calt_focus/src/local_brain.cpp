#include "local_brain.h"

#include "wearable_json.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cwchar>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

std::mutex gMu;
std::wstring gRoot;
std::wstring gServer;
std::wstring gModelPath;
HANDLE gProc = nullptr;
HANDLE gJob = nullptr;
bool gInstalled = false;
bool gRunning = false;
bool gReady = false;
int gPort = 8099;
int gCtx = 4096;
std::string gModelName = "qwen2.5-1.5b-instruct";
std::string gDetail = "not loaded";
unsigned long long gGen = 0;
unsigned long long gProbeAt = 0;

std::wstring EnvW(const wchar_t* name) {
  wchar_t buf[32768];
  DWORD n = GetEnvironmentVariableW(name, buf, 32768);
  if (n == 0 || n >= 32768) return L"";
  return buf;
}

int EnvInt(const wchar_t* name, int def, int lo, int hi) {
  std::wstring v = EnvW(name);
  if (v.empty()) return def;
  wchar_t* end = nullptr;
  long n = std::wcstol(v.c_str(), &end, 10);
  if (end == v.c_str()) return def;
  if (n < lo) return lo;
  if (n > hi) return hi;
  return (int)n;
}

bool FileExists(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDir(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

std::string FileNameUtf8(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
  if (base.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, base.data(), (int)base.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return "";
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, base.data(), (int)base.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring FindServer(const std::wstring& root) {
  std::wstring env = EnvW(L"CALT_LLAMA_SERVER");
  if (FileExists(env)) return env;
  const std::wstring dir = Join(root, L"tools\\llama");
  const std::wstring direct[] = {Join(dir, L"llama-server.exe"), Join(dir, L"llama-server")};
  for (const std::wstring& path : direct) {
    if (FileExists(path)) return path;
  }
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return L"";
  std::wstring found;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (fd.cFileName[0] == L'.') continue;
    const std::wstring nested = Join(Join(dir, fd.cFileName), L"llama-server.exe");
    if (FileExists(nested)) {
      found = nested;
      break;
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return found;
}

std::wstring FindModel(const std::wstring& root) {
  std::wstring env = EnvW(L"CALT_LLM_GGUF");
  if (FileExists(env)) return env;
  const std::wstring dir = Join(root, L"data\\models");
  const std::wstring preferred = Join(dir, L"qwen2.5-1.5b-instruct-q4_k_m.gguf");
  if (FileExists(preferred)) return preferred;
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(Join(dir, L"*.gguf").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return L"";
  std::wstring found = Join(dir, fd.cFileName);
  FindClose(h);
  return found;
}

bool HttpExchange(const wchar_t* method, int port, const wchar_t* path, const std::string& body, int recvMs,
                  int* status, std::string* out) {
  if (status) *status = 0;
  if (out) out->clear();
  HINTERNET ses = WinHttpOpen(L"CALTFocus/1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return false;
  HINTERNET con = WinHttpConnect(ses, L"127.0.0.1", (INTERNET_PORT)port, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return false;
  }
  HINTERNET req = WinHttpOpenRequest(con, method, path, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  WinHttpSetTimeouts(req, 2000, 2000, 10000, recvMs);
  const wchar_t* headers = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: application/json\r\n";
  DWORD headerLen = body.empty() ? 0 : (DWORD)-1L;
  LPVOID payload = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data();
  BOOL ok = WinHttpSendRequest(req, headers, headerLen, payload, (DWORD)body.size(), (DWORD)body.size(), 0);
  if (ok) ok = WinHttpReceiveResponse(req, nullptr);
  int code = 0;
  if (ok) {
    DWORD dw = 0;
    DWORD sz = sizeof(dw);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &dw, &sz, WINHTTP_NO_HEADER_INDEX)) {
      code = (int)dw;
    }
    if (out) {
      for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
        chunk.resize(read);
        out->append(chunk);
        if (out->size() > 1024 * 1024) break;
      }
    }
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  if (status) *status = code;
  return ok;
}

void NoteExitLocked() {
  if (!gProc) return;
  if (WaitForSingleObject(gProc, 0) != WAIT_OBJECT_0) return;
  DWORD code = 0;
  GetExitCodeProcess(gProc, &code);
  CloseHandle(gProc);
  gProc = nullptr;
  gRunning = false;
  gReady = false;
  gDetail = "llama-server exited (" + std::to_string(code) + "). See data/models/llama-server.log";
}

bool ProbeLocked(bool force) {
  NoteExitLocked();
  if (!gRunning) return false;
  const unsigned long long now = GetTickCount64();
  if (!force && gProbeAt != 0 && now - gProbeAt < 2000) return gReady;
  const int port = gPort;
  const unsigned long long gen = gGen;
  int status = 0;
  std::string body;
  const bool http = HttpExchange(L"GET", port, L"/health", "", 800, &status, &body);
  if (gen != gGen) return gReady;
  gProbeAt = GetTickCount64();
  gReady = http && status == 200;
  if (gReady) gDetail = "ready";
  else if (gRunning) gDetail = "loading";
  return gReady;
}

BrainSnap SnapLocked() {
  BrainSnap s;
  s.installed = gInstalled;
  s.running = gRunning;
  s.ready = gReady;
  s.port = gPort;
  s.ctx = gCtx;
  s.model = gModelName;
  s.detail = gDetail;
  return s;
}

std::string StatusJson(const BrainSnap& s) {
  Wj o;
  o.type = Wj::kObj;
  WjSet(o, "ok", WjBool(true));
  WjSet(o, "installed", WjBool(s.installed));
  WjSet(o, "running", WjBool(s.running));
  WjSet(o, "ready", WjBool(s.ready));
  WjSet(o, "port", WjInt(s.port));
  WjSet(o, "ctx", WjInt(s.ctx));
  WjSet(o, "model", WjStr(s.model));
  WjSet(o, "engine", WjStr("llama.cpp"));
  WjSet(o, "detail", WjStr(s.detail));
  return WjStringify(o);
}

std::string Clip(std::string s, size_t n) {
  if (s.size() > n) s.resize(n);
  return s;
}

}  // namespace

bool LocalBrainStart(const std::wstring& repoRoot) {
  std::lock_guard<std::mutex> lock(gMu);
  if (gProc) return true;
  gRoot = repoRoot;
  gPort = EnvInt(L"CALT_LLAMA_PORT", 8099, 1, 65535);
  gCtx = EnvInt(L"CALT_LLM_CTX", 4096, 512, 32768);
  const int threads = EnvInt(L"CALT_LLM_THREADS", 4, 1, 32);
  const int ngl = EnvInt(L"CALT_LLM_NGL", 0, 0, 999);
  if (EnvW(L"CALT_LLM_DISABLE") == L"1") {
    gInstalled = false;
    gRunning = false;
    gReady = false;
    gDetail = "disabled";
    return true;
  }
  gServer = FindServer(repoRoot);
  gModelPath = FindModel(repoRoot);
  gModelName = FileNameUtf8(gModelPath);
  if (gModelName.empty()) gModelName = "qwen2.5-1.5b-instruct";
  if (gServer.empty() || gModelPath.empty()) {
    gInstalled = false;
    gRunning = false;
    gReady = false;
    gDetail = "Local brain is not loaded. Run scripts\\run\\fetch_local_brain.bat, then open CALT Focus again.";
    return true;
  }
  gInstalled = true;
  const std::wstring logDir = Join(repoRoot, L"data\\models");
  EnsureDir(logDir);
  const std::wstring logPath = Join(logDir, L"llama-server.log");
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  std::wstring cmd = Quote(gServer) + L" -m " + Quote(gModelPath) + L" --host 127.0.0.1 --port " +
                     std::to_wstring(gPort) + L" -c " + std::to_wstring(gCtx) + L" -t " +
                     std::to_wstring(threads) + L" -ngl " + std::to_wstring(ngl) + L" --alias " +
                     Quote(L"qwen2.5-1.5b-instruct");
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (log != INVALID_HANDLE_VALUE && nul != INVALID_HANDLE_VALUE) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = log;
    si.hStdError = log;
  }
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(gServer.c_str(), cmdBuf.data(), nullptr, nullptr,
                           si.dwFlags & STARTF_USESTDHANDLES ? TRUE : FALSE,
                           CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, repoRoot.c_str(), &si, &pi);
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  if (!ok) {
    gRunning = false;
    gReady = false;
    gDetail = "Could not start llama-server (" + std::to_string(GetLastError()) + ").";
    return false;
  }
  if (!gJob) {
    gJob = CreateJobObjectW(nullptr, nullptr);
    if (gJob) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
      info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(gJob, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
  }
  if (gJob) AssignProcessToJobObject(gJob, pi.hProcess);
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  gProc = pi.hProcess;
  gRunning = true;
  gReady = false;
  gDetail = "loading";
  gProbeAt = 0;
  ++gGen;
  return true;
}

void LocalBrainStop() {
  std::lock_guard<std::mutex> lock(gMu);
  ++gGen;
  if (gProc) {
    TerminateProcess(gProc, 0);
    WaitForSingleObject(gProc, 3000);
    CloseHandle(gProc);
    gProc = nullptr;
  }
  if (gJob) {
    CloseHandle(gJob);
    gJob = nullptr;
  }
  gRunning = false;
  gReady = false;
  if (gInstalled) gDetail = "stopped";
}

BrainSnap LocalBrainStatus() {
  std::lock_guard<std::mutex> lock(gMu);
  ProbeLocked(false);
  return SnapLocked();
}

bool LocalBrainComplete(const std::string& system, const std::string& user, int maxTokens, std::string* text,
                        std::string* err, const char* jsonSchema, double temperature) {
  if (text) text->clear();
  if (err) err->clear();
  int port = 0;
  {
    std::lock_guard<std::mutex> lock(gMu);
    if (!ProbeLocked(true)) {
      if (err) *err = gDetail.empty() ? "Local brain is not ready" : gDetail;
      return false;
    }
    port = gPort;
  }
  if (maxTokens < 16) maxTokens = 16;
  if (maxTokens > 768) maxTokens = 768;
  if (temperature < 0) temperature = 0;
  if (temperature > 1.5) temperature = 1.5;
  Wj req;
  req.type = Wj::kObj;
  Wj msgs;
  msgs.type = Wj::kArr;
  Wj sys;
  sys.type = Wj::kObj;
  WjSet(sys, "role", WjStr("system"));
  WjSet(sys, "content", WjStr(system));
  Wj usr;
  usr.type = Wj::kObj;
  WjSet(usr, "role", WjStr("user"));
  WjSet(usr, "content", WjStr(user));
  msgs.arr.push_back(std::move(sys));
  msgs.arr.push_back(std::move(usr));
  WjSet(req, "model", WjStr("qwen2.5-1.5b-instruct"));
  WjSet(req, "messages", std::move(msgs));
  WjSet(req, "temperature", WjNum(temperature, 1));
  WjSet(req, "max_tokens", WjInt(maxTokens));
  if (jsonSchema && jsonSchema[0]) {
    Wj schema;
    std::string serr;
    if (WjParse(jsonSchema, &schema, &serr) && schema.type == Wj::kObj) {
      Wj inner;
      inner.type = Wj::kObj;
      WjSet(inner, "name", WjStr("coach"));
      WjSet(inner, "strict", WjBool(true));
      WjSet(inner, "schema", std::move(schema));
      Wj fmt;
      fmt.type = Wj::kObj;
      WjSet(fmt, "type", WjStr("json_schema"));
      WjSet(fmt, "json_schema", std::move(inner));
      WjSet(req, "response_format", std::move(fmt));
    }
  }
  int status = 0;
  std::string raw;
  const bool posted =
      HttpExchange(L"POST", port, L"/v1/chat/completions", WjStringify(req), 90000, &status, &raw);
  if (!posted) {
    if (err) *err = "Local brain request failed";
    return false;
  }
  if (status != 200 && WjGet(req, "response_format")) {
    Wj plain = req;
    // Older llama-server builds reject response_format. Ask once more as free text.
    plain.obj.erase(std::remove_if(plain.obj.begin(), plain.obj.end(),
                                    [](const std::pair<std::string, Wj>& kv) {
                                      return kv.first == "response_format";
                                    }),
                    plain.obj.end());
    status = 0;
    raw.clear();
    if (!HttpExchange(L"POST", port, L"/v1/chat/completions", WjStringify(plain), 90000, &status, &raw)) {
      if (err) *err = "Local brain request failed";
      return false;
    }
  }
  if (status != 200) {
    if (err) *err = "Local brain request failed";
    return false;
  }
  Wj doc;
  std::string perr;
  if (!WjParse(raw, &doc, &perr)) {
    if (err) *err = "Local brain returned invalid JSON";
    return false;
  }
  std::string content;
  if (const Wj* choices = WjGet(doc, "choices")) {
    if (choices->type == Wj::kArr && !choices->arr.empty() && choices->arr[0].type == Wj::kObj) {
      if (const Wj* msg = WjGet(choices->arr[0], "message")) {
        if (const Wj* c = WjGet(*msg, "content")) {
          if (c->type == Wj::kStr) content = c->str;
        }
      }
    }
  }
  if (content.empty()) {
    if (err) *err = "Local brain returned an empty reply";
    return false;
  }
  if (text) *text = content;
  return true;
}

BrainReply LocalBrainHandle(const std::string& method, const std::string& path, const std::string& body) {
  std::string p = path;
  if (p.size() > 1 && p.back() == '/') p.pop_back();
  if (method == "GET" && (p == "/api/brain" || p == "/api/brain/status")) {
    return BrainReply{200, StatusJson(LocalBrainStatus())};
  }
  if (!(method == "POST" && p == "/api/brain/chat")) {
    return BrainReply{404, "{\"ok\":false,\"detail\":\"unknown brain route\"}"};
  }
  Wj req;
  std::string perr;
  if (!WjParse(body, &req, &perr) || req.type != Wj::kObj) {
    return BrainReply{400, "{\"ok\":false,\"detail\":\"invalid JSON\"}"};
  }
  std::string prompt;
  if (const Wj* v = WjGet(req, "prompt")) {
    if (v->type == Wj::kStr) prompt = v->str;
  }
  prompt = Clip(prompt, 6000);
  if (prompt.empty()) return BrainReply{400, "{\"ok\":false,\"detail\":\"prompt required\"}"};
  std::string system = "You are CALT Focus, a concise local assistant. This model reads text only.";
  if (const Wj* v = WjGet(req, "system")) {
    if (v->type == Wj::kStr && !v->str.empty()) system = Clip(v->str, 2000);
  }
  bool wantJson = false;
  if (const Wj* v = WjGet(req, "json")) wantJson = v->type == Wj::kBool && v->b;
  if (wantJson) system += " Reply with JSON only.";
  int maxTokens = 384;
  if (const Wj* v = WjGet(req, "max_tokens")) {
    int n = 0;
    if (WjAsInt(*v, &n) && n > 0) maxTokens = n;
  }
  std::string text;
  std::string err;
  if (!LocalBrainComplete(system, prompt, maxTokens, &text, &err)) {
    BrainSnap snap = LocalBrainStatus();
    Wj o;
    o.type = Wj::kObj;
    WjSet(o, "ok", WjBool(false));
    WjSet(o, "ready", WjBool(false));
    WjSet(o, "detail", WjStr(err.empty() ? snap.detail : err));
    return BrainReply{503, WjStringify(o)};
  }
  BrainSnap snap = LocalBrainStatus();
  Wj o;
  o.type = Wj::kObj;
  WjSet(o, "ok", WjBool(true));
  WjSet(o, "ready", WjBool(true));
  WjSet(o, "text", WjStr(text));
  WjSet(o, "model", WjStr(snap.model));
  WjSet(o, "engine", WjStr("llama.cpp"));
  return BrainReply{200, WjStringify(o)};
}
