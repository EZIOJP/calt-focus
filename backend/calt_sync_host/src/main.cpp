// CALT Sync receiver. A separate process from the enforcer: it blocks on a
// socket and does nothing until the watch uploads, then writes the mirrors.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr unsigned short kPort = 8765;
constexpr const char* kToken = "calt-local-wearables";
constexpr size_t kMaxBody = 200000;

struct Mem {
  std::string key;
  std::string raw;
};

std::wstring ExeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (!n || n >= MAX_PATH) return L".";
  std::wstring p(buf, n);
  size_t slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

std::wstring DirOf(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

bool DirExists(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FindBehaviorDir() {
  wchar_t* env = _wgetenv(L"CALT_REPO");
  if (env && *env) {
    std::wstring p = Join(env, L"data\\productivity\\behavior");
    if (DirExists(p)) return p;
  }
  std::wstring dir = ExeDir();
  for (int i = 0; i < 8; ++i) {
    std::wstring p = Join(dir, L"data\\productivity\\behavior");
    if (DirExists(p)) return p;
    dir = DirOf(dir);
  }
  return L"";
}

std::string ReadFile(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool WriteFileAtomic(const std::wstring& path, const std::string& body) {
  const std::wstring tmp = path + L".tmp";
  FILE* f = nullptr;
#if defined(_MSC_VER)
  if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
#else
  f = _wfopen(tmp.c_str(), L"wb");
  if (!f) return false;
#endif
  fwrite(body.data(), 1, body.size(), f);
  fclose(f);
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#if defined(_MSC_VER)
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
#else
    f = _wfopen(path.c_str(), L"wb");
    if (!f) {
#endif
      DeleteFileW(tmp.c_str());
      return false;
    }
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    DeleteFileW(tmp.c_str());
  }
  return true;
}

std::string LocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
  return buf;
}

std::string LocalDay() {
  return LocalNow().substr(0, 10);
}

std::string Stamp() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u%02u%02u", st.wHour, st.wMinute, st.wSecond);
  return buf;
}

std::string JsonEscape(const std::string& in) {
  std::string out;
  for (unsigned char c : in) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back((char)c);
    } else if (c == '\n') {
      out += "\\n";
    } else if (c >= 0x20) {
      out.push_back((char)c);
    }
  }
  return out;
}

void SkipWs(const std::string& s, size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++*i;
}

bool ParseString(const std::string& s, size_t* i, std::string* out) {
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != '"') return false;
  ++*i;
  out->clear();
  while (*i < s.size()) {
    char c = s[(*i)++];
    if (c == '\\' && *i < s.size()) {
      out->push_back(s[(*i)++]);
      continue;
    }
    if (c == '"') return true;
    out->push_back(c);
  }
  return false;
}

bool ValueSpan(const std::string& s, size_t* i, size_t* begin, size_t* end) {
  SkipWs(s, i);
  *begin = *i;
  if (*i >= s.size()) return false;
  char c = s[*i];
  if (c == '"') {
    std::string dummy;
    if (!ParseString(s, i, &dummy)) return false;
    *end = *i;
    return true;
  }
  if (c == '{' || c == '[') {
    char open = c;
    char close = c == '{' ? '}' : ']';
    int depth = 0;
    bool inStr = false;
    for (; *i < s.size(); ++*i) {
      char ch = s[*i];
      if (inStr) {
        if (ch == '\\' && *i + 1 < s.size()) {
          ++*i;
          continue;
        }
        if (ch == '"') inStr = false;
        continue;
      }
      if (ch == '"') {
        inStr = true;
        continue;
      }
      if (ch == open) ++depth;
      else if (ch == close) {
        --depth;
        if (depth == 0) {
          ++*i;
          *end = *i;
          return true;
        }
      }
    }
    return false;
  }
  if (c == 't' || c == 'f' || c == 'n' || c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
    while (*i < s.size()) {
      char ch = s[*i];
      if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == '}' || ch == ']') break;
      ++*i;
    }
    *end = *i;
    return *end > *begin;
  }
  return false;
}

bool ParseObject(const std::string& s, std::vector<Mem>* out) {
  out->clear();
  size_t i = 0;
  SkipWs(s, &i);
  if (i >= s.size() || s[i] != '{') return false;
  ++i;
  SkipWs(s, &i);
  if (i < s.size() && s[i] == '}') return true;
  while (i < s.size()) {
    std::string key;
    if (!ParseString(s, &i, &key)) return false;
    SkipWs(s, &i);
    if (i >= s.size() || s[i] != ':') return false;
    ++i;
    size_t b = 0, e = 0;
    if (!ValueSpan(s, &i, &b, &e)) return false;
    out->push_back(Mem{key, s.substr(b, e - b)});
    SkipWs(s, &i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      continue;
    }
    if (i < s.size() && s[i] == '}') return true;
    return false;
  }
  return false;
}

std::string Emit(const std::vector<Mem>& mems) {
  std::string s = "{";
  for (size_t i = 0; i < mems.size(); ++i) {
    if (i) s += ',';
    s += '"';
    s += JsonEscape(mems[i].key);
    s += "\":";
    s += mems[i].raw;
  }
  s += '}';
  return s;
}

const Mem* FindMem(const std::vector<Mem>& mems, const char* key) {
  for (const auto& m : mems)
    if (m.key == key) return &m;
  return nullptr;
}

bool StrOf(const std::string& raw, std::string* out) {
  if (raw.size() < 2 || raw.front() != '"') return false;
  size_t i = 0;
  return ParseString(raw, &i, out);
}

bool NumOf(const std::string& raw, double* out) {
  if (raw.empty()) return false;
  char* end = nullptr;
  double v = std::strtod(raw.c_str(), &end);
  if (end == raw.c_str()) return false;
  *out = v;
  return true;
}

bool ObjRaw(const std::string& obj, const char* key, std::string* out) {
  std::vector<Mem> mems;
  if (!ParseObject(obj, &mems)) return false;
  const Mem* m = FindMem(mems, key);
  if (!m || m->raw.empty() || m->raw.front() != '{') return false;
  *out = m->raw;
  return true;
}

bool StrRaw(const std::string& obj, const char* key, std::string* out) {
  std::vector<Mem> mems;
  if (!ParseObject(obj, &mems)) return false;
  const Mem* m = FindMem(mems, key);
  if (!m) return false;
  return StrOf(m->raw, out);
}

bool NumRaw(const std::string& obj, const char* key, double* out) {
  std::vector<Mem> mems;
  if (!ParseObject(obj, &mems)) return false;
  const Mem* m = FindMem(mems, key);
  if (!m) return false;
  return NumOf(m->raw, out);
}

std::string Field(const std::string& body, const char* key) {
  std::string v;
  if (StrRaw(body, key, &v)) return v;
  std::string meta;
  if (ObjRaw(body, "meta", &meta) && StrRaw(meta, key, &v)) return v;
  return {};
}

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool AllowedSource(const std::string& source) {
  static const char* k[] = {"",
                            "mini_program",
                            "calt_sync",
                            "amazfit_calt_sync",
                            "calt_zepp",
                            "calt_zepp_raw",
                            "zepp",
                            "amazfit",
                            nullptr};
  for (int i = 0; k[i]; ++i)
    if (source == k[i]) return true;
  return false;
}

bool Foreign(const std::string& body, std::string* why) {
  std::string source = Lower(Field(body, "source"));
  static const char* bad[] = {"health_connect", "hc", "google_fit", "bridge_export", "zeppbridge",
                              nullptr};
  for (int i = 0; bad[i]; ++i) {
    if (source == bad[i]) {
      *why = source;
      return true;
    }
  }
  if (body.find("\"health_connect_records\"") != std::string::npos) {
    *why = "health_connect";
    return true;
  }
  if (!AllowedSource(source)) {
    *why = source.empty() ? "unknown" : source;
    return true;
  }
  return false;
}

std::string MergeObjects(const std::string& prior, const std::string& incoming) {
  std::vector<Mem> a, b;
  if (!ParseObject(incoming, &b)) return prior.empty() ? incoming : prior;
  if (!ParseObject(prior, &a)) return incoming;
  for (const auto& neu : b) {
    if (neu.raw == "null") continue;
    bool found = false;
    for (auto& old : a) {
      if (old.key != neu.key) continue;
      found = true;
      if (neu.key == "sleep") {
        double total = 1;
        if (NumRaw(neu.raw, "total_min", &total) && total <= 0 && !old.raw.empty() &&
            old.raw.front() == '{')
          break;
      }
      if (!old.raw.empty() && old.raw.front() == '{' && !neu.raw.empty() && neu.raw.front() == '{')
        old.raw = MergeObjects(old.raw, neu.raw);
      else
        old.raw = neu.raw;
      break;
    }
    if (!found) a.push_back(neu);
  }
  return Emit(a);
}

int RollupStudyMinutes(const std::wstring& behavior) {
  std::string roll = ReadFile(Join(behavior, L"day_rollup.json"));
  double mins = 0;
  if (NumRaw(roll, "productive_minutes", &mins)) {
    if (mins < 0) return 0;
    if (mins > 24 * 60) return 24 * 60;
    return (int)mins;
  }
  return 0;
}

std::string SummaryJson(const std::string& day, const std::string& merged, const std::string& synced) {
  std::string sleep, activity, heart, stress;
  ObjRaw(merged, "sleep", &sleep);
  ObjRaw(merged, "activity", &activity);
  ObjRaw(merged, "heart", &heart);
  ObjRaw(merged, "stress", &stress);
  double totalMin = 0, steps = 0, hr = 0, stressV = 0, score = 0;
  NumRaw(sleep, "total_min", &totalMin);
  NumRaw(sleep, "score", &score);
  NumRaw(activity, "steps", &steps);
  NumRaw(heart, "last", &hr);
  NumRaw(stress, "value", &stressV);
  double hours = totalMin > 0 ? totalMin / 60.0 : 0;
  if (hours > 16) hours = 16;
  std::string source = Field(merged, "source");
  if (source.empty()) source = "calt_zepp";
  char nums[320];
  snprintf(nums, sizeof(nums),
           ",\"sleep_hours\":%.2f,\"sleep_score\":%.0f,\"steps\":%.0f,\"hr_last\":%.0f,"
           "\"stress\":%.0f,\"source\":\"%s\",\"local_date\":\"%s\",\"synced_at\":\"%s\","
           "\"payload\":",
           hours, score, steps, hr, stressV, JsonEscape(source).c_str(), day.c_str(), synced.c_str());
  std::string out = "{\"sleep\":";
  out += sleep.empty() ? "null" : sleep;
  out += nums;
  out += merged.empty() ? "null" : merged;
  out += "}";
  return out;
}

std::string LifeJson(const std::string& day, const std::string& summary, int studyMin) {
  double hours = 0, steps = 0, hr = 0, stress = 0, score = 0;
  NumRaw(summary, "sleep_hours", &hours);
  NumRaw(summary, "steps", &steps);
  NumRaw(summary, "hr_last", &hr);
  NumRaw(summary, "stress", &stress);
  NumRaw(summary, "sleep_score", &score);
  std::string source = "calt_zepp";
  StrRaw(summary, "source", &source);
  std::string synced = LocalNow();
  StrRaw(summary, "synced_at", &synced);
  int quality = 3;
  if (score > 5) quality = (int)(score / 20.0 + 0.5);
  if (quality < 1) quality = 1;
  if (quality > 5) quality = 5;
  int exercise = 0;
  if (steps > 0) {
    exercise = (int)(steps / 100.0);
    if (exercise > 180) exercise = 180;
  }
  int stressLevel = 3;
  if (stress > 0) {
    stressLevel = (int)(stress / 20.0 + 0.5);
    if (stressLevel < 1) stressLevel = 1;
    if (stressLevel > 5) stressLevel = 5;
  }
  bool empty = hours <= 0 && steps <= 0 && stress <= 0;
  char buf[700];
  snprintf(buf, sizeof(buf),
           "{\"date\":\"%s\",\"empty\":%s,\"source\":\"%s\",\"sleep_hours\":%.2f,"
           "\"sleep_quality\":%d,\"exercise_minutes\":%d,\"water_glasses\":0,\"meals_healthy\":0,"
           "\"study_minutes\":%d,\"tasks_completed\":0,\"deep_work_blocks\":0,"
           "\"screen_time_hours\":0,\"social_media_minutes\":0,\"outdoor_minutes\":0,"
           "\"mood_score\":3,\"stress_level\":%d,\"meditation_minutes\":0,"
           "\"steps\":%.0f,\"hr_last\":%.0f,\"synced_at\":\"%s\",\"maintained_by\":\"calt_sync_host\"}",
           day.c_str(), empty ? "true" : "false", JsonEscape(source).c_str(), hours > 0 ? hours : 0,
           quality, exercise, studyMin, stressLevel, steps > 0 ? steps : 0, hr > 0 ? hr : 0,
           JsonEscape(synced).c_str());
  return buf;
}

void RecordPing(const std::wstring& behavior, const std::string& source, const std::string& message) {
  std::string at = LocalNow();
  unsigned mix = GetTickCount() ^ GetCurrentProcessId();
  char id[20];
  snprintf(id, sizeof(id), "%08x%04x", mix, GetTickCount() & 0xffff);
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"ok\":true,\"at\":\"%s\",\"id\":\"%s\",\"source\":\"%s\",\"message\":\"%s\"}", at.c_str(),
           id, JsonEscape(source).substr(0, 64).c_str(), JsonEscape(message).substr(0, 160).c_str());
  WriteFileAtomic(Join(behavior, L"wearables_ping.json"), buf);
}

std::string Ingest(const std::wstring& behavior, std::string body, bool rawRoute) {
  if (rawRoute && Field(body, "source").empty()) body = MergeObjects("{\"source\":\"calt_zepp_raw\"}", body);
  std::string why;
  if (Foreign(body, &why)) {
    return std::string("{\"ok\":false,\"detail\":\"Refused ") + JsonEscape(why) +
           ". CALT Sync is the only health ingest.\"}";
  }
  std::string day = Field(body, "local_date");
  if (day.size() < 10) day = LocalDay();
  else day = day.substr(0, 10);

  const std::wstring daysDir = Join(behavior, L"wearable_days");
  const std::wstring rawDir = Join(behavior, L"wearable_raw");
  const std::wstring lifeDir = Join(behavior, L"life_days");
  CreateDirectoryW(daysDir.c_str(), nullptr);
  CreateDirectoryW(rawDir.c_str(), nullptr);
  CreateDirectoryW(lifeDir.c_str(), nullptr);

  std::wstring dayName(day.begin(), day.end());
  std::wstring dayPath = Join(daysDir, dayName + L".json");
  std::string prior = ReadFile(dayPath);

  std::string dump = Field(body, "dump_id");
  std::string chunk = Field(body, "chunk_id");
  std::string sum = Field(body, "checksum");
  bool duplicate = !dump.empty() && !chunk.empty() && !sum.empty() && dump == Field(prior, "dump_id") &&
                   chunk == Field(prior, "chunk_id") && sum == Field(prior, "checksum");

  std::string merged = duplicate ? prior : MergeObjects(prior, body);
  std::string at = LocalNow();
  if (!duplicate) {
    std::string stamp = Stamp();
    std::wstring rawFile =
        Join(rawDir, dayName + L"_" + std::wstring(stamp.begin(), stamp.end()) + L"_ingest.json");
    std::string wrapped = std::string("{\"received_at\":\"") + at +
                          "\",\"kind\":\"ingest\",\"local_date\":\"" + day + "\",\"body\":" + body + "}";
    WriteFileAtomic(rawFile, wrapped);
    WriteFileAtomic(Join(rawDir, dayName + L"_latest.json"), wrapped);
    WriteFileAtomic(dayPath, merged);
  }

  std::string summary = SummaryJson(day, merged.empty() ? body : merged, at);
  WriteFileAtomic(Join(behavior, L"wearable_day.json"), summary);
  int study = day == LocalDay() ? RollupStudyMinutes(behavior) : 0;
  std::string life = LifeJson(day, summary, study);
  WriteFileAtomic(Join(lifeDir, dayName + L".json"), life);
  if (day == LocalDay()) WriteFileAtomic(Join(behavior, L"life_today.json"), life);

  double steps = 0, hours = 0;
  NumRaw(summary, "steps", &steps);
  NumRaw(summary, "sleep_hours", &hours);
  char sync[700];
  snprintf(sync, sizeof(sync),
           "{\"updated_at\":\"%s\",\"last_ingest_at\":\"%s\",\"last_event\":\"ingest\","
           "\"last_source\":\"%s\",\"last_local_date\":\"%s\",\"last_dump_id\":\"%s\","
           "\"last_chunk_id\":\"%s\",\"last_duplicate\":%s,\"last_steps\":%.0f,"
           "\"last_sleep_hours\":%.2f}",
           at.c_str(), at.c_str(), JsonEscape(Field(merged, "source")).c_str(), day.c_str(),
           JsonEscape(dump).c_str(), JsonEscape(chunk).c_str(), duplicate ? "true" : "false", steps,
           hours);
  WriteFileAtomic(Join(behavior, L"wearables_last_sync.json"), sync);

  bool finalChunk = true;
  std::string meta, chunkObj;
  if (ObjRaw(body, "meta", &meta) && ObjRaw(meta, "chunk", &chunkObj)) {
    double part = 0, total = 0;
    if (NumRaw(chunkObj, "part", &part) && NumRaw(chunkObj, "total", &total) && total > 0)
      finalChunk = part >= total;
  }
  if (!duplicate && finalChunk) {
    char msg[80];
    snprintf(msg, sizeof(msg), "Watch sync OK · %s · %.0f steps · %.2fh sleep", day.c_str(), steps, hours);
    RecordPing(behavior, Field(merged, "source").empty() ? "calt_zepp" : Field(merged, "source"), msg);
  }

  char reply[400];
  snprintf(reply, sizeof(reply),
           "{\"ok\":true,\"duplicate\":%s,\"local_date\":\"%s\",\"wrote_life_tracker\":true,"
           "\"steps\":%.0f,\"sleep_hours\":%.2f,\"hub\":\"calt_sync_host\"}",
           duplicate ? "true" : "false", day.c_str(), steps, hours);
  return reply;
}

std::string HeaderValue(const std::string& headers, const char* name) {
  std::string low = Lower(headers);
  std::string key = Lower(name);
  size_t p = low.find(key);
  if (p == std::string::npos) return {};
  if (p > 0 && low[p - 1] != '\n') return {};
  size_t colon = headers.find(':', p);
  if (colon == std::string::npos) return {};
  size_t i = colon + 1;
  while (i < headers.size() && (headers[i] == ' ' || headers[i] == '\t')) ++i;
  size_t end = headers.find('\r', i);
  if (end == std::string::npos) end = headers.size();
  return headers.substr(i, end - i);
}

bool Authed(const std::string& headers) {
  std::string auth = HeaderValue(headers, "authorization");
  std::string key = HeaderValue(headers, "x-calt-wearable-key");
  std::string bearer = Lower(auth);
  const std::string prefix = "bearer ";
  if (bearer.rfind(prefix, 0) == 0) auth = auth.substr(prefix.size());
  while (!auth.empty() && (auth.back() == ' ' || auth.back() == '\t')) auth.pop_back();
  return auth == kToken || key == kToken;
}

void Send(SOCKET s, int code, const std::string& json) {
  const char* text = "OK";
  if (code == 400) text = "Bad Request";
  else if (code == 401) text = "Unauthorized";
  else if (code == 404) text = "Not Found";
  else if (code == 413) text = "Payload Too Large";
  else if (code == 204) text = "No Content";
  char head[512];
  snprintf(head, sizeof(head),
           "HTTP/1.1 %d %s\r\n"
           "Content-Type: application/json; charset=utf-8\r\n"
           "Content-Length: %zu\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Headers: Authorization, Content-Type, X-CALT-Wearable-Key\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           "Connection: close\r\n\r\n",
           code, text, json.size());
  std::string msg = std::string(head) + json;
  send(s, msg.data(), (int)msg.size(), 0);
}

std::string PathOnly(const std::string& target) {
  size_t q = target.find('?');
  std::string p = q == std::string::npos ? target : target.substr(0, q);
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  return p.empty() ? "/" : p;
}

void Handle(SOCKET client, const std::wstring& behavior) {
  std::string buf;
  char tmp[4096];
  while (buf.find("\r\n\r\n") == std::string::npos && buf.size() < 65536) {
    int n = recv(client, tmp, sizeof(tmp), 0);
    if (n <= 0) return;
    buf.append(tmp, tmp + n);
  }
  size_t split = buf.find("\r\n\r\n");
  if (split == std::string::npos) {
    Send(client, 400, "{\"ok\":false,\"detail\":\"bad request\"}");
    return;
  }
  std::string head = buf.substr(0, split);
  std::string body = buf.substr(split + 4);
  size_t lineEnd = head.find("\r\n");
  std::string req = lineEnd == std::string::npos ? head : head.substr(0, lineEnd);
  std::string method, target;
  size_t sp1 = req.find(' ');
  size_t sp2 = sp1 == std::string::npos ? std::string::npos : req.find(' ', sp1 + 1);
  if (sp1 == std::string::npos) {
    Send(client, 400, "{\"ok\":false,\"detail\":\"bad request\"}");
    return;
  }
  method = req.substr(0, sp1);
  target = sp2 == std::string::npos ? req.substr(sp1 + 1) : req.substr(sp1 + 1, sp2 - sp1 - 1);
  std::string path = PathOnly(target);

  int length = 0;
  std::string len = HeaderValue(head, "content-length");
  if (!len.empty()) length = std::atoi(len.c_str());
  if (length < 0 || (size_t)length > kMaxBody) {
    Send(client, 413, "{\"ok\":false,\"detail\":\"payload too large\"}");
    return;
  }
  while ((int)body.size() < length) {
    int n = recv(client, tmp, sizeof(tmp), 0);
    if (n <= 0) return;
    body.append(tmp, tmp + n);
  }
  if ((int)body.size() > length) body.resize(length);

  printf("%s %s (%d bytes)\n", method.c_str(), path.c_str(), length);
  fflush(stdout);

  if (method == "OPTIONS") {
    Send(client, 204, "");
    return;
  }

  if (method == "GET" && (path == "/" || path == "/health")) {
    Send(client, 200,
         "{\"ok\":true,\"service\":\"calt.sync_host\",\"version\":\"1\",\"port\":8765,"
         "\"hint\":\"Amazfit Base URL http://<PC-LAN-IP>:8765\"}");
    return;
  }

  if (path.rfind("/api/wearables/zepp", 0) != 0) {
    Send(client, 404, "{\"ok\":false,\"detail\":\"not found\"}");
    return;
  }
  if (!Authed(head)) {
    Send(client, 401, "{\"ok\":false,\"detail\":\"Invalid wearable ingest key\"}");
    return;
  }

  if (method == "GET" && (path == "/api/wearables/zepp/health" || path == "/api/wearables/zepp")) {
    std::string sync = ReadFile(Join(behavior, L"wearables_last_sync.json"));
    if (sync.empty()) sync = "null";
    std::string ping = ReadFile(Join(behavior, L"wearables_ping.json"));
    if (ping.empty()) ping = "null";
    Send(client, 200,
         std::string("{\"ok\":true,\"service\":\"wearables.zepp\",\"hub\":\"calt_sync_host\","
                     "\"version\":\"1\",\"last_sync\":") +
             sync + ",\"last_ping\":" + ping + "}");
    return;
  }

  if (method == "GET" && path == "/api/wearables/zepp/ping") {
    std::string ping = ReadFile(Join(behavior, L"wearables_ping.json"));
    if (ping.empty()) ping = "null";
    Send(client, 200, std::string("{\"ok\":true,\"last_ping\":") + ping + "}");
    return;
  }

  if (method == "POST" && path == "/api/wearables/zepp/ping") {
    std::string message = "Watch ping";
    std::string source = "amazfit_calt_sync";
    std::string m, src;
    if (StrRaw(body, "message", &m) && !m.empty()) message = m;
    if (StrRaw(body, "source", &src) && !src.empty()) source = src;
    RecordPing(behavior, source, message);
    std::string ping = ReadFile(Join(behavior, L"wearables_ping.json"));
    Send(client, 200, std::string("{\"ok\":true,\"hub\":\"calt_sync_host\",\"ping\":") + ping + "}");
    return;
  }

  if (method == "POST" && (path == "/api/wearables/zepp" || path == "/api/wearables/zepp/raw")) {
    if (body.empty() || body.front() != '{') {
      Send(client, 400, "{\"ok\":false,\"detail\":\"invalid JSON\"}");
      return;
    }
    std::vector<Mem> check;
    if (!ParseObject(body, &check)) {
      Send(client, 400, "{\"ok\":false,\"detail\":\"invalid JSON\"}");
      return;
    }
    std::string reply = Ingest(behavior, body, path.find("/raw") != std::string::npos);
    Send(client, reply.find("\"ok\":false") == 0 ? 400 : 200, reply);
    return;
  }

  Send(client, 404, "{\"ok\":false,\"detail\":\"unknown wearables route\"}");
}

}  // namespace

int main() {
  std::wstring behavior = FindBehaviorDir();
  if (behavior.empty()) {
    fprintf(stderr, "Could not find data\\productivity\\behavior. Set CALT_REPO.\n");
    return 1;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) return 1;
  BOOL reuse = TRUE;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kPort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(listener, (sockaddr*)&addr, sizeof(addr)) != 0) {
    fprintf(stderr, "Port %u is already in use.\n", (unsigned)kPort);
    closesocket(listener);
    WSACleanup();
    return 1;
  }
  listen(listener, 4);
  wprintf(L"CALT Sync receiver on 0.0.0.0:%u\n", (unsigned)kPort);
  wprintf(L"  mirrors %s\n", behavior.c_str());
  wprintf(L"  idle until a watch upload\n");
  fflush(stdout);

  for (;;) {
    SOCKET client = accept(listener, nullptr, nullptr);
    if (client == INVALID_SOCKET) continue;
    Handle(client, behavior);
    closesocket(client);
  }
}
