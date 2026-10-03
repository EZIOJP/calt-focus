#include "device_block.h"

#include <windows.h>
#include <winhttp.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace {

constexpr const char* kConfirm = "DEVICE LOCK";
constexpr const wchar_t* kMutexName = L"Global\\CALT_DeviceBlockHosts";
constexpr const char* kMarkBegin = "# BEGIN CALT-DEVICE-BLOCK";
constexpr const char* kMarkEnd = "# END CALT-DEVICE-BLOCK";
constexpr const char* kMarkHeader = "# CALT device block — porn hosts (calt_enforcer)";
constexpr size_t kMaxScrapeBytes = 2 * 1024 * 1024;
constexpr DWORD kScrapeTimeoutMs = 20000;
constexpr int kMaxDomains = 8000;

const char* kPornSeeds[] = {
    "pornhub.com",     "xvideos.com",     "xnxx.com",        "xhamster.com",
    "redtube.com",     "youporn.com",     "tube8.com",       "spankbang.com",
    "chaturbate.com",  "onlyfans.com",    "porn.com",        "sex.com",
    "hentaihaven.xxx", "nhentai.net",     "rule34.xxx",      "erome.com",
    "v3.erome.com",    "eromecdn.com",    "eporner.com",     "hqporner.com",
    "porntrex.com",    "beeg.com",        "txxx.com",        "redgifs.com",
    "imagefap.com",    "motherless.com",  "fapello.com",     "missav.com",
    "jable.tv",        "thisvid.com",     "xhamster2.com",   "xhamster3.com",
    "xvideos2.com",    "xnxx.tv",         "pornhub.org",     "pornhub.net",
    nullptr,
};

const char* kWatchSeeds[] = {
    "youtube.com", "youtu.be",     "netflix.com",  "primevideo.com", "hotstar.com",
    "disneyplus.com", "hulu.com",  "twitch.tv",    "crunchyroll.com", "sonyliv.com",
    "zee5.com",    nullptr,
};

const char* kSocialSeeds[] = {
    "instagram.com", "reddit.com", "x.com", "twitter.com", "tiktok.com",
    "facebook.com",  "www.facebook.com", nullptr,
};

std::atomic<int> gRefreshState{0};  // 0 idle 1 running 2 ok 3 failed
std::string gRefreshError;
std::mutex gRefreshMu;
std::wstring gRefreshBehaviorDir;

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
  return w;
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::wstring DirOf(const std::wstring& p) {
  size_t slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

std::wstring SettingsPath(const std::wstring& behaviorDir) {
  return Join(behaviorDir, L"device_block.json");
}
std::wstring CachePath(const std::wstring& behaviorDir) {
  return Join(behaviorDir, L"porn_blocklist.json");
}
std::wstring MarkerPath(const std::wstring& behaviorDir) {
  return Join(behaviorDir, L"device_block.migrated");
}
std::wstring LegacySettingsPath(const std::wstring& behaviorDir) {
  // behaviorDir = .../data/productivity/behavior → legacy .../data/behavior/
  std::wstring prod = DirOf(behaviorDir);           // productivity
  std::wstring data = DirOf(prod);                  // data
  return Join(Join(data, L"behavior"), L"device_block.json");
}
std::wstring LegacyCachePath(const std::wstring& behaviorDir) {
  std::wstring prod = DirOf(behaviorDir);
  std::wstring data = DirOf(prod);
  return Join(Join(data, L"behavior"), L"porn_blocklist.json");
}

bool FileExists(const std::wstring& p) {
  return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ReadFileUtf8(const std::wstring& path, std::string* out) {
  std::ifstream in(Narrow(path), std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& body) {
  std::wstring tmp = path + L".tmp";
  {
    std::ofstream out(Narrow(tmp), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(body.data(), (std::streamsize)body.size());
    if (!out) return false;
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

bool CopyFileAtomic(const std::wstring& src, const std::wstring& dst) {
  std::string body;
  if (!ReadFileUtf8(src, &body)) return false;
  return WriteFileAtomic(dst, body);
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(c);
    } else if (c == '\n')
      o += "\\n";
    else if (c == '\r')
      o += "\\r";
    else
      o.push_back(c);
  }
  return o;
}

bool JsonGetBoolNear(const std::string& body, const char* key, bool* out) {
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

bool JsonGetStringNear(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size() || body[i] != '"') return false;
  ++i;
  std::string val;
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
      val.push_back(body[i + 1]);
      i += 2;
      continue;
    }
    val.push_back(body[i++]);
  }
  *out = val;
  return true;
}

std::string TrimUpper(std::string s) {
  while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
  while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

bool ConfirmOk(const std::string& confirm) { return TrimUpper(confirm) == kConfirm; }

std::string SettingsToJson(const DeviceBlockSettings& s) {
  std::ostringstream o;
  o << "{\n  \"v\": " << s.v << ",\n"
    << "  \"enabled\": " << (s.enabled ? "true" : "false") << ",\n"
    << "  \"block_porn\": " << (s.block_porn ? "true" : "false") << ",\n"
    << "  \"block_watch\": " << (s.block_watch ? "true" : "false") << ",\n"
    << "  \"block_social\": " << (s.block_social ? "true" : "false") << ",\n"
    << "  \"extra_domains\": [";
  for (size_t i = 0; i < s.extra_domains.size(); ++i) {
    if (i) o << ", ";
    o << "\"" << JsonEscape(s.extra_domains[i]) << "\"";
  }
  o << "],\n  \"source\": \"" << JsonEscape(s.source) << "\"\n}\n";
  return o.str();
}

void ParseExtraDomains(const std::string& body, std::vector<std::string>* out) {
  out->clear();
  size_t p = body.find("\"extra_domains\"");
  if (p == std::string::npos) return;
  size_t lb = body.find('[', p);
  size_t rb = body.find(']', lb);
  if (lb == std::string::npos || rb == std::string::npos) return;
  std::string arr = body.substr(lb + 1, rb - lb - 1);
  size_t i = 0;
  while (i < arr.size()) {
    while (i < arr.size() && arr[i] != '"') ++i;
    if (i >= arr.size()) break;
    ++i;
    std::string d;
    while (i < arr.size() && arr[i] != '"') {
      if (arr[i] == '\\' && i + 1 < arr.size()) {
        d.push_back(arr[i + 1]);
        i += 2;
        continue;
      }
      d.push_back(arr[i++]);
    }
    ++i;
    for (char& c : d) c = (char)std::tolower((unsigned char)c);
    if (!d.empty()) out->push_back(d);
  }
}

DeviceBlockSettings Defaults() {
  DeviceBlockSettings s;
  s.enabled = false;
  s.block_porn = true;
  s.block_watch = false;
  s.block_social = false;
  s.source = "calt_enforcer";
  return s;
}

void ParseSettingsBody(const std::string& body, DeviceBlockSettings& s) {
  s = Defaults();
  bool b = false;
  if (JsonGetBoolNear(body, "enabled", &b)) s.enabled = b;
  if (JsonGetBoolNear(body, "block_porn", &b)) s.block_porn = b;
  if (JsonGetBoolNear(body, "block_watch", &b)) s.block_watch = b;
  if (JsonGetBoolNear(body, "block_social", &b)) s.block_social = b;
  std::string src;
  if (JsonGetStringNear(body, "source", &src)) s.source = src;
  ParseExtraDomains(body, &s.extra_domains);
}

std::wstring HostsPath() {
  wchar_t windir[MAX_PATH];
  UINT n = GetWindowsDirectoryW(windir, MAX_PATH);
  if (!n) return L"C:\\Windows\\System32\\drivers\\etc\\hosts";
  return std::wstring(windir) + L"\\System32\\drivers\\etc\\hosts";
}

std::string ExpandHost(const std::string& domain) {
  std::string d = domain;
  for (char& c : d) c = (char)std::tolower((unsigned char)c);
  if (d.rfind("www.", 0) == 0) d = d.substr(4);
  if (d.empty() || d == "localhost" || d == "127.0.0.1" || d == "0.0.0.0") return {};
  return d;
}

void AddExpanded(std::set<std::string>& out, const std::string& domain) {
  std::string d = ExpandHost(domain);
  if (d.empty()) return;
  out.insert(d);
  out.insert("www." + d);
}

std::vector<std::string> LoadCacheDomains(const std::wstring& behaviorDir) {
  std::vector<std::string> domains;
  std::string body;
  if (!ReadFileUtf8(CachePath(behaviorDir), &body)) return domains;
  // naive: collect quoted strings that look like domains inside "domains" array
  size_t p = body.find("\"domains\"");
  if (p == std::string::npos) p = 0;
  size_t lb = body.find('[', p);
  size_t rb = body.rfind(']');
  if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return domains;
  std::string arr = body.substr(lb + 1, rb - lb - 1);
  size_t i = 0;
  while (i < arr.size() && (int)domains.size() < kMaxDomains) {
    while (i < arr.size() && arr[i] != '"') ++i;
    if (i >= arr.size()) break;
    ++i;
    std::string d;
    while (i < arr.size() && arr[i] != '"') {
      if (arr[i] == '\\' && i + 1 < arr.size()) {
        d.push_back(arr[i + 1]);
        i += 2;
        continue;
      }
      d.push_back(arr[i++]);
    }
    ++i;
    d = ExpandHost(d);
    if (!d.empty() && d.find('.') != std::string::npos) domains.push_back(d);
  }
  return domains;
}

std::string CacheAsOf(const std::wstring& behaviorDir) {
  std::string body, asof;
  if (!ReadFileUtf8(CachePath(behaviorDir), &body)) return {};
  if (JsonGetStringNear(body, "as_of", &asof)) return asof;
  if (JsonGetStringNear(body, "updated_at", &asof)) return asof;
  return {};
}

bool SaveCacheDomains(const std::wstring& behaviorDir, const std::vector<std::string>& domains) {
  SYSTEMTIME st;
  GetSystemTime(&st);
  char asof[64];
  snprintf(asof, sizeof(asof), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
  std::ostringstream o;
  o << "{\n  \"as_of\": \"" << asof << "\",\n  \"source\": \"theporndude.com\",\n  \"domains\": [";
  for (size_t i = 0; i < domains.size(); ++i) {
    if (i) o << ", ";
    if (i % 8 == 0) o << "\n    ";
    o << "\"" << JsonEscape(domains[i]) << "\"";
  }
  o << "\n  ]\n}\n";
  return WriteFileAtomic(CachePath(behaviorDir), o.str());
}

std::string StripManagedSection(const std::string& text, bool* unbalanced) {
  *unbalanced = false;
  if (text.find(kMarkBegin) == std::string::npos) {
    if (!text.empty() && text.back() != '\n') return text + "\n";
    return text.empty() ? std::string() : (text.back() == '\n' ? text : text + "\n");
  }
  size_t begin = text.find(kMarkBegin);
  size_t end = text.find(kMarkEnd, begin);
  if (end == std::string::npos) {
    *unbalanced = true;
    return text;
  }
  std::string before = text.substr(0, begin);
  std::string after = text.substr(end + strlen(kMarkEnd));
  while (!before.empty() && (before.back() == '\n' || before.back() == '\r')) before.pop_back();
  while (!after.empty() && (after.front() == '\n' || after.front() == '\r')) after.erase(after.begin());
  std::string out = before;
  if (!out.empty()) out += "\n";
  if (!after.empty()) {
    out += after;
    if (out.back() != '\n') out += "\n";
  } else if (!out.empty() && out.back() != '\n')
    out += "\n";
  return out;
}

std::string MergeHosts(const std::string& existing, const std::vector<std::string>& domains,
                       bool* unbalanced) {
  std::string base = StripManagedSection(existing, unbalanced);
  if (*unbalanced) return existing;
  if (domains.empty()) return base;
  std::ostringstream block;
  block << "\n" << kMarkHeader << "\n" << kMarkBegin << "\n";
  for (const auto& host : domains) {
    block << "0.0.0.0 " << host << "\n";
    block << "127.0.0.1 " << host << "\n";
    block << "::1 " << host << "\n";
  }
  block << kMarkEnd << "\n";
  if (!base.empty() && base.back() != '\n') base += "\n";
  return base + block.str();
}

bool HostsSectionActive() {
  std::string text;
  if (!ReadFileUtf8(HostsPath(), &text)) return false;
  return text.find(kMarkBegin) != std::string::npos && text.find(kMarkEnd) != std::string::npos;
}

int CountManagedEntries() {
  std::string text;
  if (!ReadFileUtf8(HostsPath(), &text)) return 0;
  size_t b = text.find(kMarkBegin);
  size_t e = text.find(kMarkEnd);
  if (b == std::string::npos || e == std::string::npos || e < b) return 0;
  std::string chunk = text.substr(b, e - b);
  std::set<std::string> hosts;
  std::istringstream ss(chunk);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string ip, host;
    if (!(ls >> ip >> host)) continue;
    if (ip == "0.0.0.0" || ip == "127.0.0.1" || ip == "::1") {
      for (char& c : host) c = (char)std::tolower((unsigned char)c);
      hosts.insert(host);
    }
  }
  return (int)hosts.size();
}

HANDLE OpenHostsMutex(DWORD waitMs, std::string* err) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  // Administrators + SYSTEM + Interactive (not Everyone/WD)
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:(A;;GA;;;BA)(A;;GA;;;SY)(A;;GA;;;IU)", SDDL_REVISION_1, &sd, nullptr)) {
    if (err) *err = "mutex_sd_failed";
    return nullptr;
  }
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = sd;
  sa.bInheritHandle = FALSE;
  HANDLE m = CreateMutexW(&sa, FALSE, kMutexName);
  LocalFree(sd);
  if (!m) {
    if (err) *err = "mutex_create_failed";
    return nullptr;
  }
  DWORD w = WaitForSingleObject(m, waitMs);
  if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) return m;
  CloseHandle(m);
  if (err) *err = "hosts_busy";
  return nullptr;
}

void ReleaseHostsMutex(HANDLE m) {
  if (!m) return;
  ReleaseMutex(m);
  CloseHandle(m);
}

void FlushDns() {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  wchar_t cmd[] = L"ipconfig /flushdns";
  if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                     &pi)) {
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
}

std::string DisableDoh() {
  HKEY key = nullptr;
  LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
                            0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
  if (rc != ERROR_SUCCESS) return "doh_needs_admin";
  DWORD zero = 0;
  RegSetValueExW(key, L"EnableAutoDoh", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
  RegCloseKey(key);
  const wchar_t* paths[] = {L"SOFTWARE\\Policies\\Microsoft\\Edge",
                            L"SOFTWARE\\Policies\\Google\\Chrome", nullptr};
  for (int i = 0; paths[i]; ++i) {
    HKEY pk = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, paths[i], 0, nullptr, 0, KEY_SET_VALUE, nullptr, &pk,
                        nullptr) == ERROR_SUCCESS) {
      const wchar_t* off = L"off";
      RegSetValueExW(pk, L"DnsOverHttpsMode", 0, REG_SZ, (const BYTE*)off,
                     (DWORD)((wcslen(off) + 1) * sizeof(wchar_t)));
      RegCloseKey(pk);
    }
  }
  return "ok";
}

bool WriteHostsFile(const std::string& content, std::string* err) {
  std::wstring path = HostsPath();
  std::ofstream out(Narrow(path), std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) *err = "access_denied";
    return false;
  }
  out.write(content.data(), (std::streamsize)content.size());
  if (!out) {
    if (err) *err = "write_failed";
    return false;
  }
  return true;
}

std::string VerifySampleJson(const std::string& hostname) {
  // Lightweight: check hosts section contains hostname; full DNS may still be cached.
  std::string text;
  ReadFileUtf8(HostsPath(), &text);
  bool inHosts = text.find(hostname) != std::string::npos;
  std::ostringstream o;
  o << "{\"hostname\":\"" << JsonEscape(hostname) << "\",\"in_hosts\":"
    << (inHosts ? "true" : "false") << ",\"blocked\":" << (inHosts ? "true" : "false") << "}";
  return o.str();
}

// --- scrape ---

bool HostAllowed(const std::wstring& host) {
  std::wstring h = host;
  for (auto& c : h) c = (wchar_t)towlower(c);
  return h == L"theporndude.com" || h == L"www.theporndude.com";
}

bool HttpGetHttps(const std::wstring& host, const std::wstring& path, std::string* body,
                  std::string* err) {
  if (!HostAllowed(host)) {
    if (err) *err = "host_not_allowed";
    return false;
  }
  HINTERNET ses = WinHttpOpen(L"CALT-DeviceBlock/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    if (err) *err = "winhttp_open";
    return false;
  }
  WinHttpSetTimeouts(ses, kScrapeTimeoutMs, kScrapeTimeoutMs, kScrapeTimeoutMs, kScrapeTimeoutMs);
  HINTERNET con = WinHttpConnect(ses, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    if (err) *err = "winhttp_connect";
    return false;
  }
  HINTERNET req =
      WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    if (err) *err = "winhttp_request";
    return false;
  }
  // Don't follow off-host redirects
  DWORD redir = WINHTTP_DISABLE_REDIRECTS;
  WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &redir, sizeof(redir));

  BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr);
  if (!ok) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    if (err) *err = "winhttp_send";
    return false;
  }
  body->clear();
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
    if (body->size() + avail > kMaxScrapeBytes) {
      if (err) *err = "response_too_large";
      WinHttpCloseHandle(req);
      WinHttpCloseHandle(con);
      WinHttpCloseHandle(ses);
      return false;
    }
    size_t old = body->size();
    body->resize(old + avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, body->data() + old, avail, &read)) break;
    body->resize(old + read);
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  return true;
}

void ExtractDomainsFromHtml(const std::string& html, std::set<std::string>& out) {
  auto addUrl = [&](const std::string& url) {
    std::string u = url;
    for (char& c : u) c = (char)std::tolower((unsigned char)c);
    size_t scheme = u.find("://");
    if (scheme == std::string::npos) return;
    size_t start = scheme + 3;
    size_t end = u.find_first_of("/?#", start);
    std::string host = (end == std::string::npos) ? u.substr(start) : u.substr(start, end - start);
    if (host.rfind("www.", 0) == 0) host = host.substr(4);
    if (host.find("theporndude") != std::string::npos) return;
    if (host.find('.') == std::string::npos) return;
    if (host.size() < 4) return;
    // skip common CDNs/social
    static const char* skip[] = {"google.", "facebook.", "twitter.", "instagram.", "youtube.",
                                 "cloudflare.", "googleapis.", nullptr};
    for (int i = 0; skip[i]; ++i)
      if (host.find(skip[i]) != std::string::npos) return;
    out.insert(host);
  };
  // href="..."
  size_t i = 0;
  while (i < html.size()) {
    size_t h = html.find("href=", i);
    if (h == std::string::npos) break;
    i = h + 5;
    while (i < html.size() && (html[i] == ' ' || html[i] == '\t')) ++i;
    if (i >= html.size()) break;
    char q = html[i];
    if (q != '"' && q != '\'') continue;
    ++i;
    size_t e = html.find(q, i);
    if (e == std::string::npos) break;
    std::string href = html.substr(i, e - i);
    i = e + 1;
    if (href.find("http://") == 0 || href.find("https://") == 0) addUrl(href);
  }
}

void ScrapeWorker(std::wstring behaviorDir, bool /*force*/) {
  gRefreshState = 1;
  {
    std::lock_guard<std::mutex> lock(gRefreshMu);
    gRefreshError.clear();
  }
  std::set<std::string> domains;
  for (int i = 0; kPornSeeds[i]; ++i) domains.insert(kPornSeeds[i]);

  const wchar_t* paths[] = {L"/", L"/top-porn-tube-sites", L"/top-premium-sites",
                            L"/free-porn-tube-sites", nullptr};
  std::string err;
  for (int i = 0; paths[i]; ++i) {
    std::string html;
    if (!HttpGetHttps(L"theporndude.com", paths[i], &html, &err)) {
      // keep going; seeds remain
      continue;
    }
    ExtractDomainsFromHtml(html, domains);
    if ((int)domains.size() >= kMaxDomains) break;
  }

  std::vector<std::string> list(domains.begin(), domains.end());
  if (list.size() > (size_t)kMaxDomains) list.resize(kMaxDomains);
  bool saved = SaveCacheDomains(behaviorDir, list);
  if (!saved) {
    std::lock_guard<std::mutex> lock(gRefreshMu);
    gRefreshError = "cache_write_failed";
    gRefreshState = 3;
    return;
  }
  if (err.size() && list.size() <= 40) {
    // scrape mostly failed; still ok with seeds
    std::lock_guard<std::mutex> lock(gRefreshMu);
    gRefreshError = err;
  }
  gRefreshState = 2;
}

}  // namespace

bool DeviceBlockEnsureMigrated(const std::wstring& behaviorDir) {
  CreateDirectoryW(behaviorDir.c_str(), nullptr);
  if (FileExists(MarkerPath(behaviorDir))) return true;

  bool copied = false;
  std::wstring legS = LegacySettingsPath(behaviorDir);
  std::wstring legC = LegacyCachePath(behaviorDir);
  if (FileExists(legS)) {
    if (!CopyFileAtomic(legS, SettingsPath(behaviorDir))) return false;
    copied = true;
  }
  if (FileExists(legC)) {
    CopyFileAtomic(legC, CachePath(behaviorDir));  // best-effort
    copied = true;
  }
  if (!copied && !FileExists(SettingsPath(behaviorDir))) {
    if (!WriteFileAtomic(SettingsPath(behaviorDir), SettingsToJson(Defaults()))) return false;
  }
  // marker only after successful settings presence
  if (!FileExists(SettingsPath(behaviorDir))) return false;
  return WriteFileAtomic(MarkerPath(behaviorDir), "migrated\n");
}

bool DeviceBlockLoadSettings(const std::wstring& behaviorDir, DeviceBlockSettings& out) {
  DeviceBlockEnsureMigrated(behaviorDir);
  std::string body;
  if (!ReadFileUtf8(SettingsPath(behaviorDir), &body)) {
    out = Defaults();
    return DeviceBlockSaveSettings(behaviorDir, out);
  }
  ParseSettingsBody(body, out);
  return true;
}

bool DeviceBlockSaveSettings(const std::wstring& behaviorDir, const DeviceBlockSettings& in) {
  DeviceBlockEnsureMigrated(behaviorDir);
  return WriteFileAtomic(SettingsPath(behaviorDir), SettingsToJson(in));
}

bool DeviceBlockTrySetEnabled(const std::wstring& behaviorDir, bool wantEnabled,
                              const std::string& confirm, std::string* errOut) {
  DeviceBlockSettings s;
  if (!DeviceBlockLoadSettings(behaviorDir, s)) {
    if (errOut) *errOut = "load_failed";
    return false;
  }
  if (s.enabled == wantEnabled) return true;
  if (!ConfirmOk(confirm)) {
    if (errOut) *errOut = confirm.empty() ? "confirm_required" : "confirm_mismatch";
    return false;
  }
  s.enabled = wantEnabled;
  s.source = "calt_enforcer";
  if (!DeviceBlockSaveSettings(behaviorDir, s)) {
    if (errOut) *errOut = "save_failed";
    return false;
  }
  return true;
}

bool DeviceBlockPatchSettings(const std::wstring& behaviorDir, const DeviceBlockSettings& patch,
                              bool hasEnabled, bool hasPorn, bool hasWatch, bool hasSocial,
                              bool hasExtra, const std::string& confirm, std::string* errOut) {
  DeviceBlockSettings s;
  if (!DeviceBlockLoadSettings(behaviorDir, s)) {
    if (errOut) *errOut = "load_failed";
    return false;
  }
  if (hasEnabled && patch.enabled != s.enabled) {
    if (!DeviceBlockTrySetEnabled(behaviorDir, patch.enabled, confirm, errOut)) return false;
    DeviceBlockLoadSettings(behaviorDir, s);
  }
  if (hasPorn) s.block_porn = patch.block_porn;
  if (hasWatch) s.block_watch = patch.block_watch;
  if (hasSocial) s.block_social = patch.block_social;
  if (hasExtra) s.extra_domains = patch.extra_domains;
  s.source = "calt_enforcer";
  if (!DeviceBlockSaveSettings(behaviorDir, s)) {
    if (errOut) *errOut = "save_failed";
    return false;
  }
  return true;
}

std::vector<std::string> DeviceBlockCollectDomains(const std::wstring& behaviorDir,
                                                   const DeviceBlockSettings& s) {
  std::set<std::string> out;
  if (s.block_porn) {
    for (int i = 0; kPornSeeds[i]; ++i) AddExpanded(out, kPornSeeds[i]);
    for (const auto& d : LoadCacheDomains(behaviorDir)) AddExpanded(out, d);
  }
  if (s.block_watch) {
    for (int i = 0; kWatchSeeds[i]; ++i) AddExpanded(out, kWatchSeeds[i]);
  }
  if (s.block_social) {
    for (int i = 0; kSocialSeeds[i]; ++i) AddExpanded(out, kSocialSeeds[i]);
  }
  for (const auto& d : s.extra_domains) AddExpanded(out, d);
  std::vector<std::string> list(out.begin(), out.end());
  if ((int)list.size() > kMaxDomains) list.resize(kMaxDomains);
  return list;
}

DeviceBlockApplyResult DeviceBlockApplyHosts(const std::wstring& behaviorDir) {
  DeviceBlockApplyResult r;
  DeviceBlockSettings s;
  if (!DeviceBlockLoadSettings(behaviorDir, s)) {
    r.error = "load_failed";
    return r;
  }
  if (!s.enabled) return DeviceBlockRemoveHosts(behaviorDir);

  std::string merr;
  HANDLE mtx = OpenHostsMutex(5000, &merr);
  if (!mtx) {
    r.error = merr.empty() ? "hosts_busy" : merr;
    return r;
  }

  auto domains = DeviceBlockCollectDomains(behaviorDir, s);
  r.domain_count = (int)domains.size();
  std::string existing;
  ReadFileUtf8(HostsPath(), &existing);
  bool unbalanced = false;
  std::string merged = MergeHosts(existing, domains, &unbalanced);
  if (unbalanced) {
    ReleaseHostsMutex(mtx);
    r.error = "hosts_unbalanced_markers";
    return r;
  }
  std::string werr;
  if (!WriteHostsFile(merged, &werr)) {
    ReleaseHostsMutex(mtx);
    r.needs_admin = (werr == "access_denied");
    r.error = werr.empty() ? "access_denied" : werr;
    return r;
  }
  FlushDns();
  r.doh_note = DisableDoh();
  ReleaseHostsMutex(mtx);
  r.ok = true;
  r.applied = true;
  std::string sample = domains.empty() ? "pornhub.com" : ExpandHost(domains[0]);
  if (sample.rfind("www.", 0) == 0) sample = sample.substr(4);
  // prefer pornhub if present
  for (const auto& d : domains)
    if (d.find("pornhub") != std::string::npos) {
      sample = ExpandHost(d);
      break;
    }
  r.verify_json = VerifySampleJson(sample.empty() ? "pornhub.com" : sample);
  return r;
}

DeviceBlockApplyResult DeviceBlockRemoveHosts(const std::wstring& behaviorDir) {
  DeviceBlockApplyResult r;
  (void)behaviorDir;
  std::string merr;
  HANDLE mtx = OpenHostsMutex(5000, &merr);
  if (!mtx) {
    r.error = merr.empty() ? "hosts_busy" : merr;
    return r;
  }
  std::string existing;
  if (!ReadFileUtf8(HostsPath(), &existing)) {
    ReleaseHostsMutex(mtx);
    r.ok = true;
    r.applied = false;
    return r;
  }
  bool unbalanced = false;
  std::string merged = MergeHosts(existing, {}, &unbalanced);
  if (unbalanced) {
    ReleaseHostsMutex(mtx);
    r.error = "hosts_unbalanced_markers";
    return r;
  }
  std::string werr;
  if (!WriteHostsFile(merged, &werr)) {
    ReleaseHostsMutex(mtx);
    r.needs_admin = (werr == "access_denied");
    r.error = werr;
    return r;
  }
  FlushDns();
  ReleaseHostsMutex(mtx);
  r.ok = true;
  r.applied = false;
  r.domain_count = 0;
  return r;
}

DeviceBlockStatusResult DeviceBlockStatus(const std::wstring& behaviorDir) {
  DeviceBlockStatusResult st;
  DeviceBlockLoadSettings(behaviorDir, st.settings);
  st.active = HostsSectionActive();
  auto domains = st.settings.enabled ? DeviceBlockCollectDomains(behaviorDir, st.settings)
                                     : std::vector<std::string>{};
  st.configured_domain_count = (int)domains.size();
  st.managed_host_entries = CountManagedEntries();
  st.needs_sync = (st.settings.enabled != st.active) ||
                  (st.settings.enabled && st.managed_host_entries > 0 &&
                   st.managed_host_entries != st.configured_domain_count);
  st.hosts_path = Narrow(HostsPath());
  st.cache_as_of = CacheAsOf(behaviorDir);
  int rs = gRefreshState.load();
  st.refresh = rs == 1 ? "running" : rs == 2 ? "ok" : rs == 3 ? "failed" : "idle";
  {
    std::lock_guard<std::mutex> lock(gRefreshMu);
    st.refresh_error = gRefreshError;
  }
  if (st.active) st.verify_json = VerifySampleJson("pornhub.com");
  return st;
}

std::string DeviceBlockRefreshListKick(const std::wstring& behaviorDir, bool force) {
  int cur = gRefreshState.load();
  if (cur == 1) return "already_running";
  if (!gRefreshState.compare_exchange_strong(cur, 1)) {
    if (gRefreshState.load() == 1) return "already_running";
    gRefreshState.store(1);
  }
  gRefreshBehaviorDir = behaviorDir;
  std::thread([behaviorDir, force]() { ScrapeWorker(behaviorDir, force); }).detach();
  return "started";
}

std::string DeviceBlockStatusJsonExtra(const DeviceBlockStatusResult& st) {
  std::ostringstream o;
  o << ",\"device_block\":{"
    << "\"settings\":{"
    << "\"enabled\":" << (st.settings.enabled ? "true" : "false") << ","
    << "\"block_porn\":" << (st.settings.block_porn ? "true" : "false") << ","
    << "\"block_watch\":" << (st.settings.block_watch ? "true" : "false") << ","
    << "\"block_social\":" << (st.settings.block_social ? "true" : "false") << ","
    << "\"source\":\"" << JsonEscape(st.settings.source) << "\""
    << "},"
    << "\"active\":" << (st.active ? "true" : "false") << ","
    << "\"configured_domain_count\":" << st.configured_domain_count << ","
    << "\"managed_host_entries\":" << st.managed_host_entries << ","
    << "\"needs_sync\":" << (st.needs_sync ? "true" : "false") << ","
    << "\"refresh\":\"" << st.refresh << "\","
    << "\"refresh_error\":\"" << JsonEscape(st.refresh_error) << "\","
    << "\"cache_as_of\":\"" << JsonEscape(st.cache_as_of) << "\","
    << "\"hosts_path\":\"" << JsonEscape(st.hosts_path) << "\"";
  if (!st.verify_json.empty()) o << ",\"verify_sample\":" << st.verify_json;
  o << "}";
  return o.str();
}

std::string DeviceBlockApplyJsonExtra(const DeviceBlockApplyResult& r) {
  std::ostringstream o;
  o << ",\"apply\":{"
    << "\"ok\":" << (r.ok ? "true" : "false") << ","
    << "\"needs_admin\":" << (r.needs_admin ? "true" : "false") << ","
    << "\"applied\":" << (r.applied ? "true" : "false") << ","
    << "\"domain_count\":" << r.domain_count << ","
    << "\"error\":\"" << JsonEscape(r.error) << "\","
    << "\"doh\":\"" << JsonEscape(r.doh_note) << "\"";
  if (!r.verify_json.empty()) o << ",\"verify_sample\":" << r.verify_json;
  o << "}";
  return o.str();
}

int DeviceBlockCliMain(const std::wstring& behaviorDir, int argc, wchar_t** argv) {
  std::string confirm;
  bool doApply = false, doRemove = false, doRefresh = false, force = false;
  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--device-block-apply") == 0) doApply = true;
    else if (wcscmp(argv[i], L"--device-block-remove") == 0) doRemove = true;
    else if (wcscmp(argv[i], L"--device-block-refresh") == 0) doRefresh = true;
    else if (wcscmp(argv[i], L"--force") == 0) force = true;
    else if (wcscmp(argv[i], L"--confirm") == 0 && i + 1 < argc) {
      confirm = Narrow(argv[++i]);
    }
  }
  DeviceBlockEnsureMigrated(behaviorDir);
  if (doRefresh) {
    // sync scrape for CLI (one-shot process)
    ScrapeWorker(behaviorDir, force);
    return gRefreshState == 2 ? 0 : 1;
  }
  if (doRemove) {
    if (!ConfirmOk(confirm)) {
      fwprintf(stderr, L"device_block remove refused: %S\n",
               confirm.empty() ? "confirm_required" : "confirm_mismatch");
      return 2;
    }
    std::string err;
    if (!DeviceBlockTrySetEnabled(behaviorDir, false, confirm, &err)) {
      fwprintf(stderr, L"device_block remove refused: %S\n", err.c_str());
      return 2;
    }
    auto r = DeviceBlockRemoveHosts(behaviorDir);
    if (!r.ok) {
      fwprintf(stderr, L"hosts remove failed: %S\n", r.error.c_str());
      return r.needs_admin ? 3 : 1;
    }
    return 0;
  }
  if (doApply) {
    DeviceBlockSettings s;
    DeviceBlockLoadSettings(behaviorDir, s);
    if (!s.enabled) {
      std::string err;
      if (!DeviceBlockTrySetEnabled(behaviorDir, true, confirm, &err)) {
        fwprintf(stderr, L"device_block enable refused: %S\n", err.c_str());
        return 2;
      }
    }
    auto r = DeviceBlockApplyHosts(behaviorDir);
    if (!r.ok) {
      fwprintf(stderr, L"hosts apply failed: %S\n", r.error.c_str());
      return r.needs_admin ? 3 : 1;
    }
    return 0;
  }
  return 1;
}
