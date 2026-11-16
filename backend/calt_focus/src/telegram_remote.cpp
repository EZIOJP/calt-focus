#include "telegram_remote.h"

#include "enforcer_cmd.h"
#include "llm_http_proxy.h"
#include "paths.h"
#include "pc_apps.h"
#include "pc_power.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mutex gMu;
TelegramRemoteConfig gCfg;
std::atomic<bool> gStop{true};
std::atomic<bool> gPolling{false};
std::thread gThread;
std::string gLastError;
DWORD gLastCmdTick = 0;
long long gLastCmdChat = 0;

std::wstring WidenUtf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string EscapeJson(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"')
      o += "\\\"";
    else if (c == '\\')
      o += "\\\\";
    else if (c == '\n')
      o += "\\n";
    else if (c == '\r')
      o += "\\r";
    else if (c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      o += buf;
    } else
      o.push_back((char)c);
  }
  return o;
}

std::wstring ConfigPath() {
  wchar_t home[MAX_PATH]{};
  if (!GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH)) return {};
  return std::wstring(home) + L"\\.calt\\telegram_remote.json";
}

bool EnsureCaltDir() {
  wchar_t home[MAX_PATH]{};
  if (!GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH)) return false;
  std::wstring dir = std::wstring(home) + L"\\.calt";
  if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return CreateDirectoryW(dir.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
  }
  return true;
}

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool WriteFileUtf8(const std::wstring& path, const std::string& body) {
  EnsureCaltDir();
  std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(body.data(), (std::streamsize)body.size());
  return (bool)out;
}

bool JsonGetBool(const std::string& j, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = j.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = j.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < j.size() && (j[i] == ' ' || j[i] == '\t')) ++i;
  if (j.compare(i, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (j.compare(i, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool JsonGetString(const std::string& j, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = j.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = j.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < j.size() && (j[i] == ' ' || j[i] == '\t')) ++i;
  if (i >= j.size() || j[i] != '"') return false;
  ++i;
  out->clear();
  while (i < j.size() && j[i] != '"') {
    if (j[i] == '\\' && i + 1 < j.size()) {
      out->push_back(j[i + 1]);
      i += 2;
      continue;
    }
    out->push_back(j[i++]);
  }
  return true;
}

bool JsonGetLongLong(const std::string& j, const char* key, long long* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = j.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = j.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < j.size() && (j[i] == ' ' || j[i] == '\t')) ++i;
  bool neg = false;
  if (i < j.size() && j[i] == '-') {
    neg = true;
    ++i;
  }
  if (i >= j.size() || j[i] < '0' || j[i] > '9') return false;
  long long v = 0;
  while (i < j.size() && j[i] >= '0' && j[i] <= '9') {
    v = v * 10 + (j[i] - '0');
    ++i;
  }
  *out = neg ? -v : v;
  return true;
}

std::vector<long long> ParseChatIdsArray(const std::string& j) {
  std::vector<long long> ids;
  size_t p = j.find("\"allowed_chat_ids\"");
  if (p == std::string::npos) return ids;
  size_t lb = j.find('[', p);
  size_t rb = j.find(']', lb == std::string::npos ? 0 : lb);
  if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return ids;
  std::string arr = j.substr(lb + 1, rb - lb - 1);
  size_t i = 0;
  while (i < arr.size()) {
    while (i < arr.size() && (arr[i] == ' ' || arr[i] == ',' || arr[i] == '\t' || arr[i] == '\n'))
      ++i;
    if (i >= arr.size()) break;
    bool neg = false;
    if (arr[i] == '-') {
      neg = true;
      ++i;
    }
    if (i >= arr.size() || arr[i] < '0' || arr[i] > '9') break;
    long long v = 0;
    while (i < arr.size() && arr[i] >= '0' && arr[i] <= '9') {
      v = v * 10 + (arr[i] - '0');
      ++i;
    }
    ids.push_back(neg ? -v : v);
  }
  return ids;
}

std::string MaskToken(const std::string& t) {
  if (t.empty()) return "";
  if (t.size() <= 8) return "****";
  return std::string(t.size() - 4, '*') + t.substr(t.size() - 4);
}

std::string ReadMirror(const char* name) {
  std::wstring path = DataBehaviorDir() + L"\\" + WidenUtf8(name);
  return ReadFileUtf8(path);
}

bool ChatAllowed(const TelegramRemoteConfig& cfg, long long chatId) {
  for (long long id : cfg.allowed_chat_ids) {
    if (id == chatId) return true;
  }
  return false;
}

bool RateOk(long long chatId) {
  const DWORD now = GetTickCount();
  if (chatId == gLastCmdChat && now - gLastCmdTick < 900) return false;
  gLastCmdChat = chatId;
  gLastCmdTick = now;
  return true;
}

std::string ToLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return s;
}

std::string Trim(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
    --b;
  return s.substr(a, b - a);
}

std::string CmdHelp(bool allowPower) {
  std::string h =
      "CALT Focus remote\n"
      "— status —\n"
      "help · status · summary · brief\n"
      "plan · todos · day\n"
      "— controls —\n"
      "softland on | softland off UNLOCK\n"
      "pass · winddown · session end\n"
      "export [days] [json|csv]  (writes productivity_export.*)\n"
      "— apps —\n"
      "close browsers · close app name.exe[,…]\n";
  if (allowPower) {
    h += "— power —\n"
         "lock · sleep\n"
         "shutdown CONFIRM · shutdown 5m CONFIRM · shutdown 120 CONFIRM\n"
         "shutdown cancel\n";
  }
  h += "Arm is not remote. Free text → OpenJarvis (if allow_chat).";
  return h;
}

std::string LocalYmd() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay);
  return buf;
}

int JsonIntNear(const std::string& j, const char* key, int def) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = j.find(needle);
  if (p == std::string::npos) return def;
  size_t colon = j.find(':', p);
  if (colon == std::string::npos) return def;
  return atoi(j.c_str() + colon + 1);
}

bool JsonBoolNear(const std::string& j, const char* key) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = j.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = j.find(':', p);
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < j.size() && (j[i] == ' ' || j[i] == '\t')) ++i;
  return j.compare(i, 4, "true") == 0;
}

std::string CmdStatus() {
  const std::string rollup = ReadMirror("day_rollup.json");
  const std::string softland = ReadMirror("softland_policy.json");
  const std::string enforcer = ReadMirror("enforcer_status.json");
  bool sl = JsonBoolNear(softland, "softland_enabled") || JsonBoolNear(softland, "enabled");
  bool armed = JsonBoolNear(enforcer, "armed");
  int prod = JsonIntNear(rollup, "productive_minutes", 0);
  if (!prod) {
    int sec = JsonIntNear(rollup, "productive_seconds", 0);
    prod = sec / 60;
  }
  int goal = JsonIntNear(rollup, "daily_focus_minutes", 0);
  char buf[192];
  if (goal > 0)
    snprintf(buf, sizeof(buf), "SoftLand %s · Arm %s · Focus %dm / %dm", sl ? "ON" : "OFF",
             armed ? "ARMED" : "off", prod, goal);
  else
    snprintf(buf, sizeof(buf), "SoftLand %s · Arm %s · Focus %dm", sl ? "ON" : "OFF",
             armed ? "ARMED" : "off", prod);
  return buf;
}

std::string EnforcerOpRaw(const std::string& op, const std::string& payload, unsigned timeoutMs,
                          std::string* respOut) {
  std::string req = std::string("{\"op\":\"") + op + "\",\"v\":1,\"id\":\"tg\",\"payload\":" +
                    payload + "}";
  std::string resp;
  if (!EnforcerSendCommand(req, resp, timeoutMs)) return "enforcer unreachable";
  if (respOut) *respOut = resp;
  if (resp.find("\"ok\":true") != std::string::npos) return "ok";
  std::string err;
  if (JsonGetString(resp, "error", &err) && !err.empty()) return err;
  return "failed";
}

std::string EnforcerOp(const std::string& op, const std::string& payload) {
  return EnforcerOpRaw(op, payload, 8000, nullptr);
}

std::string CmdSummary() {
  std::string line = CmdStatus();
  const std::string loop = ReadMirror("day_loop.json");
  const std::string life = ReadMirror("life_today.json");
  bool bible = JsonBoolNear(loop, "bible_done");
  bool plan = JsonBoolNear(loop, "plan_confirmed");
  int openTodos = 0;
  {
    size_t p = 0;
    while ((p = loop.find("\"done\":false", p)) != std::string::npos) {
      ++openTodos;
      p += 12;
    }
  }
  int steps = JsonIntNear(life, "steps", 0);
  double sleepH = 0;
  {
    size_t p = life.find("\"sleep_hours\"");
    if (p != std::string::npos) {
      size_t colon = life.find(':', p);
      if (colon != std::string::npos) sleepH = atof(life.c_str() + colon + 1);
    }
  }
  std::string dayResp;
  EnforcerOpRaw("day.status", "{}", 6000, &dayResp);
  int passToday = JsonBoolNear(dayResp, "pass_today") ? 1 : 0;
  int rewardAvail = JsonIntNear(dayResp, "reward_available", -1);

  std::ostringstream ss;
  ss << "Summary " << LocalYmd() << "\n" << line << "\n";
  ss << "Bible " << (bible ? "done" : "open") << " · Plan " << (plan ? "confirmed" : "pending");
  ss << " · Todos open " << openTodos << "\n";
  if (steps > 0 || sleepH > 0.05)
    ss << "Life: sleep " << sleepH << "h · steps " << steps << "\n";
  if (rewardAvail >= 0) ss << "Pass today " << (passToday ? "yes" : "no") << " · Rewards "
                           << rewardAvail << "\n";
  ss << "SoftLand ≠ Arm.";
  return ss.str();
}

std::string CmdPlanToday() {
  const std::string ymd = LocalYmd();
  const std::string blocks = ReadMirror("plan_blocks.json");
  if (blocks.empty()) return "No plan mirror — open Focus Plan once.";
  // Collect titles whose start_at contains today's date (local or Z).
  std::vector<std::string> titles;
  size_t pos = 0;
  while (titles.size() < 12) {
    size_t tkey = blocks.find("\"title\"", pos);
    if (tkey == std::string::npos) break;
    size_t colon = blocks.find(':', tkey);
    size_t q1 = blocks.find('"', colon == std::string::npos ? 0 : colon + 1);
    if (q1 == std::string::npos) break;
    size_t q2 = q1 + 1;
    std::string title;
    while (q2 < blocks.size() && blocks[q2] != '"') {
      if (blocks[q2] == '\\' && q2 + 1 < blocks.size()) {
        title.push_back(blocks[q2 + 1]);
        q2 += 2;
        continue;
      }
      title.push_back(blocks[q2++]);
    }
    size_t startKey = blocks.find("\"start_at\"", q2);
    size_t nextTitle = blocks.find("\"title\"", q2);
    if (startKey != std::string::npos && (nextTitle == std::string::npos || startKey < nextTitle)) {
      size_t sc = blocks.find(':', startKey);
      size_t sq1 = blocks.find('"', sc == std::string::npos ? 0 : sc + 1);
      std::string start;
      if (sq1 != std::string::npos) {
        size_t sq2 = sq1 + 1;
        while (sq2 < blocks.size() && blocks[sq2] != '"') start.push_back(blocks[sq2++]);
      }
      if (start.find(ymd) != std::string::npos && !title.empty()) titles.push_back(title);
    }
    pos = q2 + 1;
  }
  if (titles.empty()) return "No plan blocks for " + ymd + ".";
  std::ostringstream ss;
  ss << "Plan " << ymd << " (" << titles.size() << ")\n";
  for (size_t i = 0; i < titles.size(); ++i) ss << (i + 1) << ". " << titles[i] << "\n";
  return Trim(ss.str());
}

std::string CmdTodos() {
  const std::string loop = ReadMirror("day_loop.json");
  if (loop.empty()) return "No day_loop mirror.";
  std::vector<std::string> open;
  size_t pos = 0;
  while (open.size() < 15) {
    size_t tkey = loop.find("\"title\"", pos);
    if (tkey == std::string::npos) break;
    size_t colon = loop.find(':', tkey);
    size_t q1 = loop.find('"', colon == std::string::npos ? 0 : colon + 1);
    if (q1 == std::string::npos) break;
    size_t q2 = q1 + 1;
    std::string title;
    while (q2 < loop.size() && loop[q2] != '"') {
      if (loop[q2] == '\\' && q2 + 1 < loop.size()) {
        title.push_back(loop[q2 + 1]);
        q2 += 2;
        continue;
      }
      title.push_back(loop[q2++]);
    }
    size_t doneKey = loop.find("\"done\"", q2);
    size_t nextTitle = loop.find("\"title\"", q2);
    bool done = false;
    if (doneKey != std::string::npos && (nextTitle == std::string::npos || doneKey < nextTitle)) {
      size_t dc = loop.find(':', doneKey);
      if (dc != std::string::npos) {
        size_t i = dc + 1;
        while (i < loop.size() && (loop[i] == ' ' || loop[i] == '\t')) ++i;
        done = loop.compare(i, 4, "true") == 0;
      }
    }
    if (!done && !title.empty()) open.push_back(title);
    pos = q2 + 1;
  }
  if (open.empty()) return "No open todos.";
  std::ostringstream ss;
  ss << "Todos (" << open.size() << ")\n";
  for (size_t i = 0; i < open.size(); ++i) ss << (i + 1) << ". " << open[i] << "\n";
  return Trim(ss.str());
}

std::string CmdExport(int days, const std::string& format) {
  if (days < 1) days = 1;
  if (days > 90) days = 90;
  std::string fmt = format;
  if (fmt != "csv" && fmt != "csv_wide") fmt = "json";
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"days\":%d,\"format\":\"%s\",\"skip_empty\":true}", days,
           fmt.c_str());
  std::string resp;
  std::string r = EnforcerOpRaw("productivity.export", payload, 60000, &resp);
  if (r != "ok") return "Export failed: " + r;
  std::string files;
  JsonGetString(resp, "json_path", &files);
  if (files.empty()) JsonGetString(resp, "csv_path", &files);
  // Prefer short message with range from response.
  int rangeDays = JsonIntNear(resp, "days", days);
  std::ostringstream ss;
  ss << "Export OK (" << rangeDays << "d, " << fmt << ").\n"
     << "Files under data/productivity/behavior/productivity_export.*";
  return ss.str();
}

/** Parse delay like 5m, 10m, 1h, or raw seconds. Returns 0 on failure. */
unsigned ParseDelaySeconds(const std::string& tok) {
  if (tok.empty()) return 0;
  char* end = nullptr;
  long v = strtol(tok.c_str(), &end, 10);
  if (v < 0) return 0;
  if (end && *end) {
    if (*end == 'm' || *end == 'M') return (unsigned)(v * 60);
    if (*end == 'h' || *end == 'H') return (unsigned)(v * 3600);
    if (*end == 's' || *end == 'S') return (unsigned)v;
    return 0;
  }
  // bare number: if <= 180 treat as minutes when used with "shutdown 5 CONFIRM"? Plan said 5m.
  // Bare number = seconds for clarity with shutdown.exe.
  return (unsigned)v;
}

std::string CmdSoftland(bool on, const std::string& confirm) {
  if (!on) {
    if (ToLower(confirm) != "unlock") return "To turn SoftLand off: softland off UNLOCK";
    std::string r = EnforcerOp("softland.set_enabled", "{\"enabled\":false,\"confirm\":\"UNLOCK\"}");
    return r == "ok" ? "SoftLand off." : ("SoftLand: " + r);
  }
  std::string r = EnforcerOp("softland.set_enabled", "{\"enabled\":true}");
  return r == "ok" ? "SoftLand on." : ("SoftLand: " + r);
}

std::string CmdPass() {
  std::string r = EnforcerOp("day.grant_pass", "{}");
  if (r == "ok") return "Day pass granted.";
  if (r == "bible_required") return "Bible first — then day pass.";
  return "Day pass: " + r;
}

std::string CmdBrief() {
  // Prefer OpenJarvis one-shot; fall back to status line.
  const std::string body =
      "{\"model\":\"qwen3.5:4b\",\"messages\":["
      "{\"role\":\"system\",\"content\":\"You are Jarvis for CALT Focus. One short spoken "
      "sentence. SoftLand != Arm. No markdown.\"},"
      "{\"role\":\"user\",\"content\":\"Give a tiny day brief. Status: " +
      EscapeJson(CmdStatus()) + "\"}],\"max_tokens\":80,\"temperature\":0.5}";
  LlmHttpResult r = LlmHttpRequest("POST", "http://127.0.0.1:8000/v1/chat/completions", "",
                                   "application/json", body, 45000);
  if (!r.ok || r.status < 200 || r.status >= 300) return CmdStatus() + " (OpenJarvis offline)";
  size_t c = r.body.find("\"content\"");
  if (c == std::string::npos) return CmdStatus();
  size_t q1 = r.body.find('"', r.body.find(':', c) + 1);
  if (q1 == std::string::npos) return CmdStatus();
  size_t q2 = q1 + 1;
  std::string text;
  while (q2 < r.body.size() && r.body[q2] != '"') {
    if (r.body[q2] == '\\' && q2 + 1 < r.body.size()) {
      text.push_back(r.body[q2 + 1]);
      q2 += 2;
      continue;
    }
    text.push_back(r.body[q2++]);
  }
  text = Trim(text);
  return text.empty() ? CmdStatus() : text;
}

std::string CmdChat(const std::string& user) {
  const std::string body =
      "{\"model\":\"qwen3.5:4b\",\"messages\":["
      "{\"role\":\"system\",\"content\":\"You are Jarvis for CALT Focus. Short answers. "
      "SoftLand != Arm. You cannot change SoftLand/Arm unless they use typed commands.\"},"
      "{\"role\":\"user\",\"content\":\"" +
      EscapeJson(user) + "\"}],\"max_tokens\":220,\"temperature\":0.55}";
  LlmHttpResult r = LlmHttpRequest("POST", "http://127.0.0.1:8000/v1/chat/completions", "",
                                   "application/json", body, 90000);
  if (!r.ok) return "OpenJarvis unreachable — type help.";
  if (r.status < 200 || r.status >= 300) return "OpenJarvis error — type help.";
  size_t c = r.body.find("\"content\"");
  if (c == std::string::npos) return "Empty reply.";
  size_t q1 = r.body.find('"', r.body.find(':', c) + 1);
  if (q1 == std::string::npos) return "Empty reply.";
  size_t q2 = q1 + 1;
  std::string text;
  while (q2 < r.body.size() && r.body[q2] != '"') {
    if (r.body[q2] == '\\' && q2 + 1 < r.body.size()) {
      char n = r.body[q2 + 1];
      if (n == 'n')
        text.push_back('\n');
      else
        text.push_back(n);
      q2 += 2;
      continue;
    }
    text.push_back(r.body[q2++]);
  }
  text = Trim(text);
  return text.empty() ? "Empty reply." : text;
}

struct TgHttpResult {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
};

TgHttpResult TelegramHttp(const std::string& methodPath, const std::string& postJson,
                          unsigned timeoutMs) {
  TgHttpResult out;
  HINTERNET ses = WinHttpOpen(L"CALT-Focus-Telegram/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    out.error = "winhttp_open";
    return out;
  }
  if (timeoutMs < 5000) timeoutMs = 5000;
  if (timeoutMs > 120000) timeoutMs = 120000;
  WinHttpSetTimeouts(ses, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs);
  HINTERNET con = WinHttpConnect(ses, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    out.error = "winhttp_connect";
    return out;
  }
  std::wstring path = WidenUtf8(methodPath);
  HINTERNET req =
      WinHttpOpenRequest(con, postJson.empty() ? L"GET" : L"POST", path.c_str(), nullptr,
                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    out.error = "winhttp_request";
    return out;
  }
  BOOL okSend = TRUE;
  if (postJson.empty()) {
    okSend = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                                0, 0);
  } else {
    std::wstring hdr = L"Content-Type: application/json\r\n";
    okSend = WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L, (LPVOID)postJson.data(),
                                (DWORD)postJson.size(), (DWORD)postJson.size(), 0);
  }
  if (!okSend || !WinHttpReceiveResponse(req, nullptr)) {
    out.error = "winhttp_send";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return out;
  }
  DWORD status = 0;
  DWORD sz = sizeof(status);
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
  out.status = (int)status;
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
    if (out.body.size() + avail > 2 * 1024 * 1024) break;
    size_t old = out.body.size();
    out.body.resize(old + avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, out.body.data() + old, avail, &read)) break;
    out.body.resize(old + read);
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  out.ok = true;
  return out;
}

bool SendMessageTg(const std::string& token, long long chatId, const std::string& text) {
  if (token.empty() || text.empty()) return false;
  std::string path = "/bot" + token + "/sendMessage";
  std::string body = std::string("{\"chat_id\":") + std::to_string(chatId) + ",\"text\":\"" +
                     EscapeJson(text.substr(0, 3500)) + "\"}";
  TgHttpResult r = TelegramHttp(path, body, 20000);
  return r.ok && r.status >= 200 && r.status < 300;
}

void SaveConfigUnlocked(const TelegramRemoteConfig& cfg) {
  std::ostringstream ss;
  ss << "{\n  \"enabled\": " << (cfg.enabled ? "true" : "false") << ",\n  \"bot_token\": \""
     << EscapeJson(cfg.bot_token) << "\",\n  \"allowed_chat_ids\": [";
  for (size_t i = 0; i < cfg.allowed_chat_ids.size(); ++i) {
    if (i) ss << ", ";
    ss << cfg.allowed_chat_ids[i];
  }
  ss << "],\n  \"allow_chat\": " << (cfg.allow_chat ? "true" : "false")
     << ",\n  \"allow_power\": " << (cfg.allow_power ? "true" : "false")
     << ",\n  \"link_next\": " << (cfg.link_next ? "true" : "false") << ",\n  \"offset\": "
     << cfg.offset << "\n}\n";
  WriteFileUtf8(ConfigPath(), ss.str());
}

void PollerMain() {
  gPolling = true;
  {
    std::lock_guard<std::mutex> lock(gMu);
    gLastError.clear();
  }
  while (!gStop.load()) {
    TelegramRemoteConfig cfg;
    {
      std::lock_guard<std::mutex> lock(gMu);
      cfg = gCfg;
    }
    if (!cfg.enabled || cfg.bot_token.empty()) break;

    std::string path = "/bot" + cfg.bot_token + "/getUpdates?timeout=25&offset=" +
                       std::to_string(cfg.offset);
    TgHttpResult r = TelegramHttp(path, "", 35000);
    if (gStop.load()) break;
    if (!r.ok) {
      std::lock_guard<std::mutex> lock(gMu);
      gLastError = r.error.empty() ? "poll_failed" : r.error;
      Sleep(3000);
      continue;
    }
    if (r.status == 401 || r.status == 404) {
      std::lock_guard<std::mutex> lock(gMu);
      gLastError = "bad_bot_token";
      Sleep(10000);
      continue;
    }

    // Walk result updates — naive scan for "update_id" and message text/chat id.
    size_t pos = 0;
    long long maxOffset = cfg.offset;
    while (true) {
      size_t uid = r.body.find("\"update_id\"", pos);
      if (uid == std::string::npos) break;
      long long updateId = 0;
      {
        size_t colon = r.body.find(':', uid);
        if (colon != std::string::npos) updateId = _strtoi64(r.body.c_str() + colon + 1, nullptr, 10);
      }
      if (updateId + 1 > maxOffset) maxOffset = updateId + 1;

      size_t nextUid = r.body.find("\"update_id\"", uid + 10);
      std::string chunk =
          r.body.substr(uid, nextUid == std::string::npos ? std::string::npos : nextUid - uid);

      long long chatId = 0;
      size_t chat = chunk.find("\"chat\"");
      if (chat != std::string::npos) {
        size_t idKey = chunk.find("\"id\"", chat);
        if (idKey != std::string::npos) {
          size_t colon = chunk.find(':', idKey);
          if (colon != std::string::npos)
            chatId = _strtoi64(chunk.c_str() + colon + 1, nullptr, 10);
        }
      }
      std::string text;
      size_t tkey = chunk.find("\"text\"");
      if (tkey != std::string::npos) {
        size_t colon = chunk.find(':', tkey);
        size_t q1 = chunk.find('"', colon == std::string::npos ? 0 : colon + 1);
        if (q1 != std::string::npos) {
          size_t q2 = q1 + 1;
          while (q2 < chunk.size() && chunk[q2] != '"') {
            if (chunk[q2] == '\\' && q2 + 1 < chunk.size()) {
              text.push_back(chunk[q2 + 1]);
              q2 += 2;
              continue;
            }
            text.push_back(chunk[q2++]);
          }
        }
      }
      pos = uid + 10;
      if (chatId == 0 || text.empty()) continue;

      std::string reply = TelegramRemoteDispatch(chatId, text);
      if (!reply.empty()) SendMessageTg(cfg.bot_token, chatId, reply);

      // Reload cfg after possible link_next mutation
      {
        std::lock_guard<std::mutex> lock(gMu);
        cfg = gCfg;
      }
    }

    if (maxOffset > cfg.offset) {
      std::lock_guard<std::mutex> lock(gMu);
      gCfg.offset = maxOffset;
      SaveConfigUnlocked(gCfg);
    }
  }
  gPolling = false;
}

}  // namespace

TelegramRemoteConfig TelegramRemoteLoad() {
  TelegramRemoteConfig cfg;
  std::string body = ReadFileUtf8(ConfigPath());
  if (body.empty()) return cfg;
  JsonGetBool(body, "enabled", &cfg.enabled);
  JsonGetString(body, "bot_token", &cfg.bot_token);
  JsonGetBool(body, "allow_chat", &cfg.allow_chat);
  JsonGetBool(body, "allow_power", &cfg.allow_power);
  JsonGetBool(body, "link_next", &cfg.link_next);
  JsonGetLongLong(body, "offset", &cfg.offset);
  cfg.allowed_chat_ids = ParseChatIdsArray(body);
  return cfg;
}

bool TelegramRemoteSave(const TelegramRemoteConfig& cfg, std::string* err) {
  if (!EnsureCaltDir()) {
    if (err) *err = "mkdir_failed";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(gMu);
    gCfg = cfg;
    SaveConfigUnlocked(gCfg);
  }
  return true;
}

void TelegramRemoteStop() {
  gStop = true;
  // getUpdates blocks ~25s — don't join on UI/quit path.
  if (gThread.joinable()) gThread.detach();
  gPolling = false;
}

void TelegramRemoteApply(const TelegramRemoteConfig& cfg) {
  gStop = true;
  if (gThread.joinable()) gThread.detach();
  {
    std::lock_guard<std::mutex> lock(gMu);
    gCfg = cfg;
    SaveConfigUnlocked(gCfg);
  }
  if (!cfg.enabled || cfg.bot_token.empty()) {
    gPolling = false;
    return;
  }
  gStop = false;
  gThread = std::thread(PollerMain);
}

bool TelegramRemoteIsPolling() { return gPolling.load(); }

std::string TelegramRemoteStatusJson() {
  TelegramRemoteConfig cfg;
  std::string lastErr;
  {
    std::lock_guard<std::mutex> lock(gMu);
    cfg = gCfg;
    lastErr = gLastError;
  }
  if (cfg.bot_token.empty()) {
    // If never applied this session, load from disk.
    cfg = TelegramRemoteLoad();
    std::lock_guard<std::mutex> lock(gMu);
    if (gCfg.bot_token.empty()) gCfg = cfg;
  }
  std::ostringstream ss;
  ss << "{\"enabled\":" << (cfg.enabled ? "true" : "false") << ",\"polling\":"
     << (TelegramRemoteIsPolling() ? "true" : "false") << ",\"bot_token_masked\":\""
     << EscapeJson(MaskToken(cfg.bot_token)) << "\",\"has_token\":"
     << (cfg.bot_token.empty() ? "false" : "true") << ",\"allow_chat\":"
     << (cfg.allow_chat ? "true" : "false") << ",\"allow_power\":"
     << (cfg.allow_power ? "true" : "false") << ",\"link_next\":"
     << (cfg.link_next ? "true" : "false") << ",\"allowed_chat_ids\":[";
  for (size_t i = 0; i < cfg.allowed_chat_ids.size(); ++i) {
    if (i) ss << ",";
    ss << cfg.allowed_chat_ids[i];
  }
  ss << "]";
  if (!lastErr.empty()) ss << ",\"last_error\":\"" << EscapeJson(lastErr) << "\"";
  ss << "}";
  return ss.str();
}

std::string TelegramRemoteDispatch(long long chatId, const std::string& textRaw) {
  TelegramRemoteConfig cfg;
  {
    std::lock_guard<std::mutex> lock(gMu);
    cfg = gCfg;
  }
  if (!cfg.enabled) return {};

  std::string text = Trim(textRaw);
  if (text.empty()) return {};

  // Link flow: Settings sets link_next; any chat that says link|/start is allowlisted.
  if (cfg.link_next) {
    std::string low = ToLower(text);
    if (low == "link" || low == "/start" || low == "start" || low.rfind("/start ", 0) == 0) {
      bool already = ChatAllowed(cfg, chatId);
      {
        std::lock_guard<std::mutex> lock(gMu);
        if (!already) gCfg.allowed_chat_ids.push_back(chatId);
        gCfg.link_next = false;
        SaveConfigUnlocked(gCfg);
        cfg = gCfg;
      }
      char buf[96];
      snprintf(buf, sizeof(buf), "Linked chat %lld. Type help.", (long long)chatId);
      return buf;
    }
  }

  if (!ChatAllowed(cfg, chatId)) {
    // Stay quiet — don't advertise the bot to strangers.
    return {};
  }
  if (!RateOk(chatId)) return "Slow down — one command per second.";

  if (!text.empty() && text[0] == '/') text = text.substr(1);
  std::string low = ToLower(text);
  // strip @botname from /start@Bot
  size_t at = low.find('@');
  if (at != std::string::npos && at < 12) {
    size_t sp = low.find(' ');
    if (sp == std::string::npos || at < sp) {
      low = low.substr(0, at) + (sp == std::string::npos ? "" : low.substr(sp));
      text = text.substr(0, at) + (sp == std::string::npos ? "" : text.substr(sp));
      low = ToLower(Trim(low));
      text = Trim(text);
    }
  }

  std::vector<std::string> parts;
  {
    std::istringstream iss(low);
    std::string p;
    while (iss >> p) parts.push_back(p);
  }
  const std::string head = parts.empty() ? "" : parts[0];

  if (head == "help" || head == "start" || head == "?") return CmdHelp(cfg.allow_power);
  if (head == "status" || head == "st") return CmdStatus();
  if (head == "summary" || head == "sum" || head == "day") return CmdSummary();
  if (head == "brief" || head == "b") return CmdBrief();
  if (head == "plan" || head == "agenda") return CmdPlanToday();
  if (head == "todos" || head == "todo" || head == "tasks") return CmdTodos();
  if (head == "pass" || head == "daypass") return CmdPass();
  if (head == "winddown" || head == "wind") {
    std::string r = EnforcerOp("softland.emergency_winddown", "{}");
    return r == "ok" ? "Emergency wind-down set." : ("Wind-down: " + r);
  }
  if (head == "session" && parts.size() >= 2 && (parts[1] == "end" || parts[1] == "stop")) {
    std::string r = EnforcerOp("session.set", "{\"active\":false,\"apps\":[],\"sites\":[]}");
    return r == "ok" ? "Work session ended." : ("Session: " + r);
  }
  if (head == "export") {
    int days = 7;
    std::string fmt = "json";
    for (size_t i = 1; i < parts.size(); ++i) {
      if (parts[i] == "csv" || parts[i] == "json" || parts[i] == "csv_wide")
        fmt = parts[i];
      else {
        int d = atoi(parts[i].c_str());
        if (d > 0) days = d;
      }
    }
    return CmdExport(days, fmt);
  }
  if (head == "close" || head == "kill") {
    if (parts.size() >= 2 && (parts[1] == "browsers" || parts[1] == "browser")) {
      PcAppsResult r = PcAppsCloseBrowsers();
      return r.detail;
    }
    if (parts.size() >= 2 && (parts[1] == "app" || parts[1] == "apps")) {
      std::vector<std::string> exes;
      // "close app discord.exe,spotify.exe" or "close app discord spotify"
      if (parts.size() == 2) return "Usage: close app name.exe[,other.exe]";
      std::string rest = text;
      size_t appPos = ToLower(rest).find("app");
      if (appPos != std::string::npos) rest = Trim(rest.substr(appPos + 3));
      for (char& c : rest) {
        if (c == ',' || c == ';') c = ' ';
      }
      std::istringstream iss(rest);
      std::string tok;
      while (iss >> tok) exes.push_back(tok);
      if (exes.empty()) return "Usage: close app name.exe[,other.exe]";
      PcAppsResult r = PcAppsCloseNamed(exes);
      return r.detail;
    }
    if (head == "kill" && parts.size() >= 2) {
      std::vector<std::string> exes(parts.begin() + 1, parts.end());
      PcAppsResult r = PcAppsCloseNamed(exes);
      return r.detail;
    }
    return "Usage: close browsers | close app name.exe";
  }
  if (head == "softland" || head == "sl") {
    if (parts.size() >= 2 && parts[1] == "on") return CmdSoftland(true, "");
    if (parts.size() >= 2 && parts[1] == "off")
      return CmdSoftland(false, parts.size() >= 3 ? parts[2] : "");
    return "Usage: softland on | softland off UNLOCK";
  }
  if (head == "link") {
    return "Open Focus Settings → Telegram → Allow next chat, then send link again.";
  }
  if (cfg.allow_power) {
    if (head == "lock") {
      PcPowerResult r = PcPowerRun(PcPowerOp::Lock);
      return r.ok ? "PC locked." : ("Lock failed: " + r.detail);
    }
    if (head == "sleep") {
      PcPowerResult r = PcPowerRun(PcPowerOp::Sleep);
      return r.ok ? "PC sleeping." : ("Sleep failed: " + r.detail);
    }
    if (head == "shutdown" || head == "off") {
      if (parts.size() >= 2 && (parts[1] == "cancel" || parts[1] == "abort")) {
        PcPowerResult r = PcPowerRun(PcPowerOp::Cancel);
        return r.ok ? r.detail : ("Cancel failed: " + r.detail);
      }
      // shutdown CONFIRM | shutdown 5m CONFIRM | shutdown in 10m CONFIRM | shutdown 120 CONFIRM
      bool hasConfirm = false;
      unsigned delaySec = 30;
      for (size_t i = 1; i < parts.size(); ++i) {
        if (parts[i] == "confirm") {
          hasConfirm = true;
          continue;
        }
        if (parts[i] == "in") continue;
        unsigned d = ParseDelaySeconds(parts[i]);
        if (d > 0) delaySec = d;
      }
      if (!hasConfirm) {
        return "Usage: shutdown CONFIRM | shutdown 5m CONFIRM | shutdown cancel";
      }
      PcPowerResult r = PcPowerShutdownIn(delaySec);
      return r.ok ? r.detail : ("Shutdown failed: " + r.detail);
    }
  } else if (head == "lock" || head == "sleep" || head == "shutdown" || head == "off") {
    return "Power commands disabled in Settings.";
  }

  if (!cfg.allow_chat) return "Chat disabled — type help for commands.";
  return CmdChat(text);
}
