#include "wearable_store.h"

#include "wearable_json.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr size_t kMaxDumpBytes = 8 * 1024 * 1024;

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        o += "\\\"";
        break;
      case '\\':
        o += "\\\\";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      case '\t':
        o += "\\t";
        break;
      default:
        if (c < 0x20) continue;
        o.push_back((char)c);
    }
  }
  return o;
}

std::string UtcNow() {
  SYSTEMTIME st;
  GetSystemTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
  return buf;
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

bool EnsureDir(const std::wstring& path) {
  if (path.empty()) return false;
  DWORD attr = GetFileAttributesW(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadFileCap(const std::wstring& path, std::string* out) {
  out->clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || (unsigned long long)sz.QuadPart > kMaxDumpBytes) {
    CloseHandle(h);
    return false;
  }
  out->resize((size_t)sz.QuadPart);
  DWORD read = 0;
  BOOL ok = TRUE;
  if (!out->empty()) ok = ReadFile(h, out->data(), (DWORD)out->size(), &read, nullptr);
  CloseHandle(h);
  if (!ok || read != out->size()) {
    out->clear();
    return false;
  }
  return true;
}

bool WriteAtomic(const std::wstring& dest, const std::string& body) {
  if (dest.empty() || body.empty()) return false;
  const std::wstring tmp = dest + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = WriteFile(h, body.data(), (DWORD)body.size(), &written, nullptr);
  CloseHandle(h);
  if (!ok || written != body.size()) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(dest.c_str());
    if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      DeleteFileW(tmp.c_str());
      return false;
    }
  }
  return true;
}

std::wstring FullPath(const std::wstring& path) {
  wchar_t buf[4096];
  DWORD n = GetFullPathNameW(path.c_str(), 4096, buf, nullptr);
  if (n == 0 || n >= 4096) return path;
  return buf;
}

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) return L"";
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
  return w;
}

bool SpoolAllowed(const std::wstring& behaviorDir, const std::wstring& spool, std::wstring* fullOut) {
  // Prefer this enforcer's inbox. Also accept Focus's inbox if the two processes
  // resolved data\behavior differently — the dump is still written into this store.
  std::wstring full = FullPath(spool);
  if (full.find(L"..") != std::wstring::npos) return false;
  const std::wstring own = FullPath(Join(behaviorDir, L"wearable_inbox"));
  bool underOwn = false;
  if (!own.empty() && full.size() > own.size() && _wcsnicmp(full.c_str(), own.c_str(), own.size()) == 0) {
    wchar_t sep = full[own.size()];
    underOwn = sep == L'\\' || sep == L'/';
  }
  std::wstring leaf = full;
  size_t slash = leaf.find_last_of(L"\\/");
  if (slash != std::wstring::npos) leaf = leaf.substr(slash + 1);
  if (leaf.size() < 8 || _wcsnicmp(leaf.c_str(), L"in_", 3) != 0) return false;
  if (_wcsicmp(leaf.c_str() + leaf.size() - 5, L".json") != 0) return false;
  if (!underOwn) {
    std::wstring folder = full.substr(0, slash);
    size_t parent = folder.find_last_of(L"\\/");
    std::wstring name = parent == std::wstring::npos ? folder : folder.substr(parent + 1);
    if (_wcsicmp(name.c_str(), L"wearable_inbox") != 0) return false;
  }
  DWORD attr = GetFileAttributesW(full.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
  *fullOut = full;
  return true;
}

const Wj* ObjChild(const Wj& obj, const char* key) {
  const Wj* v = WjGet(obj, key);
  if (!v || v->type != Wj::kObj) return nullptr;
  return v;
}

bool ChildInt(const Wj& obj, const char* key, int* out) {
  const Wj* v = WjGet(obj, key);
  if (!v) return false;
  return WjAsInt(*v, out);
}

std::string ChildStr(const Wj& obj, const char* key) {
  const Wj* v = WjGet(obj, key);
  std::string s;
  if (v && WjAsString(*v, &s)) return s;
  return "";
}

std::string ScalarOf(const Wj& obj, const char* key) {
  const Wj* v = WjGet(obj, key);
  if (!v) return "";
  if (v->type == Wj::kStr) return v->str;
  if (v->type == Wj::kNum) return v->num;
  return "";
}

std::string IdOf(const Wj& body, const char* key) {
  std::string s = ScalarOf(body, key);
  if (!s.empty()) return s;
  const Wj* meta = ObjChild(body, "meta");
  if (!meta) return "";
  return ScalarOf(*meta, key);
}

bool IsWatchSource(const std::string& source) {
  return source == "mini_program" || source == "zepp" || source == "amazfit" ||
         source == "health_connect" || source == "hc" || source == "google_fit" ||
         source == "bridge_export" || source == "zeppbridge";
}

bool SameId(const Wj& body, const char* key, const std::string& val) {
  return !val.empty() && IdOf(body, key) == val;
}

int SittingMin(const Wj& merged) {
  int n = 0;
  const Wj* sitting = ObjChild(merged, "sitting");
  if (sitting) {
    if (ChildInt(*sitting, "minutes", &n) || ChildInt(*sitting, "min", &n) ||
        ChildInt(*sitting, "sitting_min", &n) || ChildInt(*sitting, "value", &n)) {
      return n;
    }
  }
  const Wj* activity = ObjChild(merged, "activity");
  if (activity && ChildInt(*activity, "sitting_min", &n)) return n;
  return -1;
}

int WorkoutMin(const Wj& merged) {
  const Wj* workouts = WjGet(merged, "workouts");
  if (!workouts || workouts->type != Wj::kArr) return 0;
  int sum = 0;
  for (const Wj& item : workouts->arr) {
    if (item.type != Wj::kObj) continue;
    int m = 0;
    if (ChildInt(item, "duration_min", &m) || ChildInt(item, "minutes", &m)) {
      if (m > 0) sum += m;
    }
  }
  return sum;
}

int Clamp15(int score) {
  if (score <= 0) return 3;
  int q = (score + 10) / 20;
  if (q < 1) q = 1;
  if (q > 5) q = 5;
  return q;
}

std::string OptInt(bool present, int v) { return present ? std::to_string(v) : "null"; }

std::string OptNum(bool present, double v) {
  if (!present) return "null";
  char buf[64];
  snprintf(buf, sizeof(buf), "%.2f", v);
  return buf;
}

std::string OptStr(const std::string& s) {
  if (s.empty()) return "null";
  return "\"" + JsonEscape(s) + "\"";
}

bool LoadDay(const std::wstring& path, Wj* out) {
  std::string raw;
  if (!ReadFileCap(path, &raw) || raw.empty()) return false;
  std::string err;
  if (!WjParse(raw, out, &err) || out->type != Wj::kObj) return false;
  return true;
}

Wj BuildSummary(const std::string& day, const Wj& merged, const std::string& receivedAt) {
  Wj s;
  s.type = Wj::kObj;
  WjSet(s, "local_date", WjStr(day));
  std::string source = ChildStr(merged, "source");
  WjSet(s, "source", source.empty() ? WjNull() : WjStr(source));
  WjSet(s, "synced_at", WjStr(receivedAt));
  WjSet(s, "last_received_at", WjStr(receivedAt));
  WjSet(s, "captured_at", ChildStr(merged, "captured_at").empty()
                              ? WjNull()
                              : WjStr(ChildStr(merged, "captured_at")));
  WjSet(s, "receiver", WjStr("calt_enforcer"));

  int sleepScore = 0;
  int deep = 0;
  bool hasScore = false;
  bool hasDeep = false;
  double hours = 0;
  bool hasHours = false;
  if (const Wj* sleep = ObjChild(merged, "sleep")) {
    int total = 0;
    if (ChildInt(*sleep, "total_min", &total) && total > 0) {
      hours = total / 60.0;
      if (hours > 16.0) hours = 16.0;
      hasHours = true;
    }
    hasScore = ChildInt(*sleep, "score", &sleepScore);
    hasDeep = ChildInt(*sleep, "deep_min", &deep);
  }
  WjSet(s, "sleep_hours", hasHours ? WjNum(hours, 2) : WjNull());
  WjSet(s, "sleep_score", hasScore ? WjInt(sleepScore) : WjNull());
  WjSet(s, "sleep_deep_min", hasDeep ? WjInt(deep) : WjNull());

  int steps = 0, stepTarget = 0, kcal = 0, kcalTarget = 0, dist = 0, hr = 0, hrRest = 0;
  int spo2 = 0, stress = 0, stand = 0, standTarget = 0, battery = 0;
  bool hasSteps = false, hasStepTarget = false, hasKcal = false, hasKcalTarget = false;
  bool hasDist = false, hasHr = false, hasHrRest = false, hasSpo2 = false, hasStress = false;
  bool hasStand = false, hasStandTarget = false, hasBattery = false;
  if (const Wj* activity = ObjChild(merged, "activity")) {
    hasSteps = ChildInt(*activity, "steps", &steps);
    hasStepTarget = ChildInt(*activity, "target", &stepTarget);
  }
  if (const Wj* calorie = ObjChild(merged, "calorie")) {
    hasKcal = ChildInt(*calorie, "kcal", &kcal);
    hasKcalTarget = ChildInt(*calorie, "target", &kcalTarget);
  }
  if (const Wj* distance = ObjChild(merged, "distance")) hasDist = ChildInt(*distance, "meters", &dist);
  if (const Wj* heart = ObjChild(merged, "heart")) {
    hasHr = ChildInt(*heart, "last", &hr);
    hasHrRest = ChildInt(*heart, "resting", &hrRest);
  }
  if (const Wj* ox = ObjChild(merged, "spo2")) {
    hasSpo2 = ChildInt(*ox, "value", &spo2) || ChildInt(*ox, "last_day_avg", &spo2);
  }
  if (const Wj* st = ObjChild(merged, "stress")) hasStress = ChildInt(*st, "value", &stress);
  if (const Wj* sd = ObjChild(merged, "stand")) {
    hasStand = ChildInt(*sd, "hours", &stand);
    hasStandTarget = ChildInt(*sd, "target", &standTarget);
  }
  if (const Wj* bat = ObjChild(merged, "battery")) hasBattery = ChildInt(*bat, "pct", &battery);

  double paiToday = 0, paiTotal = 0;
  bool hasPaiToday = false, hasPaiTotal = false;
  if (const Wj* pai = ObjChild(merged, "pai")) {
    const Wj* t = WjGet(*pai, "today");
    if (t && t->type == Wj::kNum) {
      paiToday = strtod(t->num.c_str(), nullptr);
      hasPaiToday = true;
    }
    const Wj* tot = WjGet(*pai, "total");
    if (tot && tot->type == Wj::kNum) {
      paiTotal = strtod(tot->num.c_str(), nullptr);
      hasPaiTotal = true;
    }
  }
  int sitting = SittingMin(merged);

  WjSet(s, "steps", hasSteps ? WjInt(steps) : WjNull());
  WjSet(s, "step_target", hasStepTarget ? WjInt(stepTarget) : WjNull());
  WjSet(s, "calories", hasKcal ? WjInt(kcal) : WjNull());
  WjSet(s, "calorie_target", hasKcalTarget ? WjInt(kcalTarget) : WjNull());
  WjSet(s, "distance_m", hasDist ? WjInt(dist) : WjNull());
  WjSet(s, "hr_last", hasHr ? WjInt(hr) : WjNull());
  WjSet(s, "hr_resting", hasHrRest ? WjInt(hrRest) : WjNull());
  WjSet(s, "spo2", hasSpo2 ? WjInt(spo2) : WjNull());
  WjSet(s, "stress", hasStress ? WjInt(stress) : WjNull());
  WjSet(s, "pai_today", hasPaiToday ? WjNum(paiToday, 2) : WjNull());
  WjSet(s, "pai_total", hasPaiTotal ? WjNum(paiTotal, 2) : WjNull());
  WjSet(s, "stand_hours", hasStand ? WjInt(stand) : WjNull());
  WjSet(s, "stand_target", hasStandTarget ? WjInt(standTarget) : WjNull());
  WjSet(s, "battery_pct", hasBattery ? WjInt(battery) : WjNull());
  WjSet(s, "sitting_min", sitting >= 0 ? WjInt(sitting) : WjNull());
  WjSet(s, "last_dump_id", IdOf(merged, "dump_id").empty() ? WjNull() : WjStr(IdOf(merged, "dump_id")));
  WjSet(s, "last_chunk_id",
        IdOf(merged, "chunk_id").empty() ? WjNull() : WjStr(IdOf(merged, "chunk_id")));
  WjSet(s, "payload", merged);
  return s;
}

std::string LifeJson(const std::string& day, const Wj& merged, const std::string& receivedAt,
                     const std::string& source) {
  int steps = 0;
  bool hasSteps = false;
  if (const Wj* activity = ObjChild(merged, "activity")) hasSteps = ChildInt(*activity, "steps", &steps);
  int exercise = -1;
  if (hasSteps && steps >= 0) {
    exercise = steps / 100;
    if (exercise > 180) exercise = 180;
  }
  int workouts = WorkoutMin(merged);
  if (workouts > 0) exercise = workouts;
  else if (const Wj* active = ObjChild(merged, "active_minutes")) {
    int m = 0;
    if (ChildInt(*active, "minutes", &m)) exercise = m;
  }
  int stress = 0;
  bool hasStress = false;
  if (const Wj* st = ObjChild(merged, "stress")) hasStress = ChildInt(*st, "value", &stress);
  double hours = 0;
  bool hasHours = false;
  int sleepScore = 0;
  bool hasScore = false;
  if (const Wj* sleep = ObjChild(merged, "sleep")) {
    int total = 0;
    if (ChildInt(*sleep, "total_min", &total) && total > 0) {
      hours = total / 60.0;
      if (hours > 16.0) hours = 16.0;
      hasHours = true;
    }
    hasScore = ChildInt(*sleep, "score", &sleepScore);
  }
  bool empty = !hasHours && !hasSteps && exercise < 0 && !hasStress;
  int hr = 0, spo2 = 0;
  bool hasHr = false, hasSpo2 = false;
  if (const Wj* heart = ObjChild(merged, "heart")) hasHr = ChildInt(*heart, "last", &hr);
  if (const Wj* ox = ObjChild(merged, "spo2")) {
    hasSpo2 = ChildInt(*ox, "value", &spo2) || ChildInt(*ox, "last_day_avg", &spo2);
  }
  std::string src = source.empty() ? "google_fit" : source;
  std::string body = std::string("{\"date\":\"") + day + "\",\"empty\":" + (empty ? "true" : "false") +
                     ",\"source\":\"" + JsonEscape(src) + "\",\"sleep_hours\":" + OptNum(hasHours, hours) +
                     ",\"sleep_quality\":" + std::to_string(hasScore ? Clamp15(sleepScore) : 3) +
                     ",\"exercise_minutes\":" + std::to_string(exercise < 0 ? 0 : exercise) +
                     ",\"water_glasses\":0,\"meals_healthy\":0,\"study_minutes\":0,\"tasks_completed\":0,"
                     "\"deep_work_blocks\":0,\"screen_time_hours\":0,\"social_media_minutes\":0,"
                     "\"outdoor_minutes\":0,\"mood_score\":3,\"stress_level\":" +
                     std::to_string(hasStress && stress > 0 ? Clamp15(stress) : 3) +
                     ",\"meditation_minutes\":0,\"steps\":" + OptInt(hasSteps, steps) +
                     ",\"hr_last\":" + OptInt(hasHr, hr) + ",\"spo2\":" + OptInt(hasSpo2, spo2) +
                     ",\"synced_at\":\"" + JsonEscape(receivedAt) + "\"}";
  // sleep_hours is 0 in the Python mirror when missing; keep that for the Health screen.
  if (!hasHours) {
    body = std::string("{\"date\":\"") + day + "\",\"empty\":" + (empty ? "true" : "false") +
           ",\"source\":\"" + JsonEscape(src) +
           "\",\"sleep_hours\":0,\"sleep_quality\":" + std::to_string(hasScore ? Clamp15(sleepScore) : 3) +
           ",\"exercise_minutes\":" + std::to_string(exercise < 0 ? 0 : exercise) +
           ",\"water_glasses\":0,\"meals_healthy\":0,\"study_minutes\":0,\"tasks_completed\":0,"
           "\"deep_work_blocks\":0,\"screen_time_hours\":0,\"social_media_minutes\":0,"
           "\"outdoor_minutes\":0,\"mood_score\":3,\"stress_level\":" +
           std::to_string(hasStress && stress > 0 ? Clamp15(stress) : 3) +
           ",\"meditation_minutes\":0,\"steps\":" + OptInt(hasSteps, steps) +
           ",\"hr_last\":" + OptInt(hasHr, hr) + ",\"spo2\":" + OptInt(hasSpo2, spo2) +
           ",\"synced_at\":\"" + JsonEscape(receivedAt) + "\"}";
  }
  return body;
}

std::string SyncJson(const std::string& receivedAt, const std::string& source, bool watch,
                     const std::string& day, const Wj& merged, bool duplicate) {
  int steps = 0;
  bool hasSteps = false;
  if (const Wj* activity = ObjChild(merged, "activity")) hasSteps = ChildInt(*activity, "steps", &steps);
  double hours = 0;
  bool hasHours = false;
  int score = 0;
  bool hasScore = false;
  if (const Wj* sleep = ObjChild(merged, "sleep")) {
    int total = 0;
    if (ChildInt(*sleep, "total_min", &total) && total > 0) {
      hours = total / 60.0;
      if (hours > 16.0) hours = 16.0;
      hasHours = true;
    }
    hasScore = ChildInt(*sleep, "score", &score);
  }
  return std::string("{\"updated_at\":\"") + receivedAt + "\",\"last_ingest_at\":\"" + receivedAt +
         "\",\"last_received_at\":\"" + receivedAt +
         "\",\"last_event\":\"ingest\",\"last_source\":" + OptStr(source) +
         ",\"last_is_watch\":" + (watch ? "true" : "false") +
         ",\"last_wrote_life\":" + (watch ? "true" : "false") + ",\"last_local_date\":\"" + day +
         "\",\"last_dump_id\":" + OptStr(IdOf(merged, "dump_id")) +
         ",\"last_chunk_id\":" + OptStr(IdOf(merged, "chunk_id")) +
         ",\"last_captured_at\":" + OptStr(ChildStr(merged, "captured_at")) +
         ",\"last_duplicate\":" + (duplicate ? "true" : "false") +
         ",\"last_steps\":" + OptInt(hasSteps, steps) +
         ",\"last_sleep_hours\":" + OptNum(hasHours, hours) +
         ",\"last_sleep_quality\":" + OptInt(hasScore, score) +
         ",\"receiver\":\"calt_enforcer\",\"via\":\"calt_focus\"}";
}

std::string ReceiptFragment(const std::string& receivedAt, bool duplicate, const std::string& day,
                            const std::string& source, bool watch) {
  return std::string(",\"duplicate\":") + (duplicate ? "true" : "false") +
         ",\"wrote_life_tracker\":true,\"local_date\":\"" + day + "\",\"source\":" + OptStr(source) +
         ",\"last_received_at\":\"" + receivedAt + "\",\"received\":true,\"watch_received\":" +
         (watch ? "true" : "false") + ",\"stored_by\":\"calt_enforcer\"";
}

bool LoadDocument(const std::wstring& behaviorDir, const Wj& envelope, Wj* doc, std::string* err) {
  const Wj* spool = WjGet(envelope, "spool");
  if (spool) {
    std::string path;
    if (!WjAsString(*spool, &path) || path.empty()) {
      *err = "bad_payload";
      return false;
    }
    std::wstring full;
    if (!SpoolAllowed(behaviorDir, Utf8ToWide(path), &full)) {
      *err = "spool_rejected";
      return false;
    }
    std::string raw;
    if (!ReadFileCap(full, &raw)) {
      *err = "spool_missing";
      return false;
    }
    DeleteFileW(full.c_str());
    std::string perr;
    if (!WjParse(raw, doc, &perr) || doc->type != Wj::kObj) {
      *err = "bad_payload";
      return false;
    }
    return true;
  }
  if (envelope.type != Wj::kObj) {
    *err = "bad_payload";
    return false;
  }
  *doc = envelope;
  return true;
}

}  // namespace

void WearableStatusExtra(const std::wstring& behaviorDir, std::string* extraOut) {
  if (!extraOut) return;
  std::string raw;
  Wj sync;
  std::string received;
  std::string source;
  bool watch = false;
  bool have = ReadFileCap(Join(behaviorDir, L"wearables_last_sync.json"), &raw);
  std::string err;
  if (have && WjParse(raw, &sync, &err) && sync.type == Wj::kObj) {
    received = ChildStr(sync, "last_received_at");
    if (received.empty()) received = ChildStr(sync, "last_ingest_at");
    source = ChildStr(sync, "last_source");
    const Wj* flag = WjGet(sync, "last_is_watch");
    watch = flag && flag->type == Wj::kBool && flag->b;
    if (!watch) watch = IsWatchSource(source);
  }
  if (received.empty()) {
    *extraOut =
        ",\"last_received_at\":null,\"received\":false,\"watch_received\":false,\"last_source\":"
        "null,\"stored_by\":\"calt_enforcer\"";
    return;
  }
  *extraOut = ",\"last_received_at\":\"" + JsonEscape(received) +
              "\",\"received\":true,\"watch_received\":" +
              ((watch && source != "web_test" && !source.empty()) ? "true" : "false") +
              ",\"last_source\":" + OptStr(source) + ",\"stored_by\":\"calt_enforcer\"";
}

std::string WearableIngest(const std::wstring& behaviorDir, const std::string& payload,
                           std::string* extraOut) {
  if (behaviorDir.empty()) return "no_behavior_dir";
  std::string err;
  Wj envelope;
  if (!WjParse(payload, &envelope, &err) || envelope.type != Wj::kObj) return "bad_payload";
  Wj doc;
  if (!LoadDocument(behaviorDir, envelope, &doc, &err)) return err;

  std::string day = ChildStr(doc, "local_date");
  if (!IsYmd(day)) day = LocalToday();
  std::string source = ChildStr(doc, "source");
  bool watch = IsWatchSource(source);

  if (!EnsureDir(behaviorDir)) return "write_failed";
  std::wstring daysDir = Join(behaviorDir, L"wearable_days");
  std::wstring lifeDir = Join(behaviorDir, L"life_days");
  if (!EnsureDir(daysDir) || !EnsureDir(lifeDir)) return "write_failed";

  std::wstring dayPath = Join(daysDir, Utf8ToWide(day) + L".json");
  Wj prior;
  bool hadPrior = LoadDay(dayPath, &prior);
  std::string dumpId = IdOf(doc, "dump_id");
  std::string chunkId = IdOf(doc, "chunk_id");
  std::string checksum = IdOf(doc, "checksum");
  bool duplicate = hadPrior && !dumpId.empty() && !chunkId.empty() && !checksum.empty() &&
                   SameId(prior, "dump_id", dumpId) && SameId(prior, "chunk_id", chunkId) &&
                   SameId(prior, "checksum", checksum);

  Wj merged = hadPrior ? prior : Wj{};
  if (merged.type != Wj::kObj) {
    merged = Wj{};
    merged.type = Wj::kObj;
  }
  if (!duplicate) {
    WjMergeDay(&merged, doc);
    if (!dumpId.empty()) WjSet(merged, "dump_id", WjStr(dumpId));
    if (!chunkId.empty()) WjSet(merged, "chunk_id", WjStr(chunkId));
    if (!checksum.empty()) WjSet(merged, "checksum", WjStr(checksum));
    WjSet(merged, "local_date", WjStr(day));
    if (!WriteAtomic(dayPath, WjStringify(merged))) return "write_failed";
  }

  const std::string receivedAt = UtcNow();
  if (!WriteAtomic(Join(behaviorDir, L"wearables_last_sync.json"),
                   SyncJson(receivedAt, source, watch, day, duplicate ? prior : merged, duplicate))) {
    return "write_failed";
  }
  if (!duplicate) {
    Wj summary = BuildSummary(day, merged, receivedAt);
    if (!WriteAtomic(Join(behaviorDir, L"wearable_day.json"), WjStringify(summary))) return "write_failed";
    std::string life = LifeJson(day, merged, receivedAt, source);
    if (!WriteAtomic(Join(lifeDir, Utf8ToWide(day) + L".json"), life)) return "write_failed";
    if (day == LocalToday()) {
      if (!WriteAtomic(Join(behaviorDir, L"life_today.json"), life)) return "write_failed";
    }
  }
  if (extraOut) {
    *extraOut = ReceiptFragment(receivedAt, duplicate, day, source, watch && source != "web_test");
  }
  return "";
}
