#include "nutrition_host.h"

#include "wearable_json.h"

#include <windows.h>
#include <winhttp.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Macro {
  const char* name;
  double kcal, p, c, f, fiber;
};

const Macro kFoods[] = {
    {"chicken biryani", 1.85, 0.10, 0.18, 0.08, 0.01},
    {"vegetable biryani", 1.50, 0.04, 0.22, 0.05, 0.02},
    {"masala dosa", 1.97, 0.04, 0.25, 0.08, 0.01},
    {"plain dosa", 1.68, 0.04, 0.27, 0.04, 0.01},
    {"idli", 0.58, 0.02, 0.12, 0.00, 0.01},
    {"sambar", 0.52, 0.03, 0.08, 0.01, 0.02},
    {"dal tadka", 0.99, 0.07, 0.12, 0.03, 0.04},
    {"paneer butter masala", 1.50, 0.09, 0.07, 0.10, 0.01},
    {"roti", 2.97, 0.09, 0.53, 0.04, 0.05},
    {"chapati", 2.97, 0.09, 0.53, 0.04, 0.05},
    {"naan", 3.10, 0.09, 0.56, 0.07, 0.02},
    {"rice (cooked)", 1.30, 0.03, 0.28, 0.00, 0.00},
    {"cooked rice", 1.30, 0.03, 0.28, 0.00, 0.00},
    {"chole", 1.64, 0.09, 0.27, 0.03, 0.08},
    {"palak paneer", 1.25, 0.08, 0.05, 0.09, 0.02},
    {"aloo gobi", 0.85, 0.02, 0.12, 0.04, 0.02},
    {"poha", 1.80, 0.03, 0.34, 0.05, 0.01},
    {"upma", 1.50, 0.04, 0.27, 0.04, 0.02},
    {"egg", 1.55, 0.13, 0.01, 0.11, 0.00},
    {"omelette", 1.54, 0.10, 0.01, 0.12, 0.00},
    {"apple", 0.52, 0.00, 0.14, 0.00, 0.02},
    {"banana", 0.89, 0.01, 0.23, 0.00, 0.03},
    {"salad", 0.20, 0.01, 0.03, 0.00, 0.02},
    {"burger", 2.50, 0.12, 0.25, 0.13, 0.01},
    {"pizza", 2.66, 0.11, 0.33, 0.10, 0.02},
    {"french fries", 3.12, 0.04, 0.41, 0.15, 0.04},
    {"coffee", 0.02, 0.00, 0.00, 0.00, 0.00},
    {"milk tea", 0.40, 0.02, 0.05, 0.02, 0.00},
    {"curd", 0.60, 0.04, 0.05, 0.03, 0.00},
    {"unknown", 1.50, 0.05, 0.20, 0.07, 0.02},
};

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::string Lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return s;
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

std::string LocalClock() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

int TzOffsetMin() {
  TIME_ZONE_INFORMATION tzi{};
  GetTimeZoneInformation(&tzi);
  return -(int)tzi.Bias;
}

bool EnsureDir(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadFileCap(const std::wstring& path, std::string* out, size_t maxBytes) {
  out->clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || (unsigned long long)sz.QuadPart > maxBytes) {
    CloseHandle(h);
    return false;
  }
  out->resize((size_t)sz.QuadPart);
  DWORD read = 0;
  BOOL ok = TRUE;
  if (!out->empty()) ok = ReadFile(h, out->data(), (DWORD)out->size(), &read, nullptr);
  CloseHandle(h);
  return ok && read == out->size();
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

void AppendEvent(const std::wstring& path, const std::string& line) {
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  std::string row = line + "\n";
  DWORD written = 0;
  WriteFile(h, row.data(), (DWORD)row.size(), &written, nullptr);
  CloseHandle(h);
}

struct PerG {
  double kcal, p, c, f, fiber;
  const char* source;
};

const Macro* FindFood(const std::string& name);

PerG EstimatePerG(const std::string& name) {
  const Macro* food = FindFood(name);
  const bool known = std::string(food->name) != "unknown";
  return {food->kcal, food->p, food->c, food->f, food->fiber, known ? "local" : "fallback"};
}

const Macro* FindFood(const std::string& name) {
  const std::string key = Lower(name);
  const Macro* unknown = &kFoods[0];
  for (const Macro& food : kFoods) {
    if (std::string(food.name) == "unknown") unknown = &food;
    if (key == food.name) return &food;
  }
  return unknown;
}

double Round1(double v) { return std::round(v * 10.0) / 10.0; }

std::string ChildStr(const Wj& obj, const char* key) {
  const Wj* v = WjGet(obj, key);
  std::string s;
  if (v && WjAsString(*v, &s)) return s;
  return "";
}

double ChildNum(const Wj& obj, const char* key) {
  const Wj* v = WjGet(obj, key);
  if (!v || v->type != Wj::kNum || v->num.empty()) return 0;
  return strtod(v->num.c_str(), nullptr);
}

std::wstring DayPath(const std::wstring& nutri, const std::string& day) {
  return Join(Join(nutri, L"days"), std::wstring(day.begin(), day.end()) + L".json");
}

Wj LoadDay(const std::wstring& path, const std::string& day) {
  Wj doc;
  doc.type = Wj::kObj;
  std::string raw;
  std::string err;
  if (ReadFileCap(path, &raw, 2 * 1024 * 1024) && WjParse(raw, &doc, &err) && doc.type == Wj::kObj) {
    return doc;
  }
  Wj empty;
  empty.type = Wj::kObj;
  WjSet(empty, "date", WjStr(day));
  Wj meals;
  meals.type = Wj::kArr;
  WjSet(empty, "meals", std::move(meals));
  return empty;
}

Wj MealsOf(const Wj& day) {
  if (const Wj* m = WjGet(day, "meals")) {
    if (m->type == Wj::kArr) return *m;
  }
  Wj meals;
  meals.type = Wj::kArr;
  return meals;
}

Wj TotalsOf(const Wj& meals) {
  double kcal = 0, p = 0, c = 0, f = 0, fiber = 0;
  int count = 0;
  for (const Wj& meal : meals.arr) {
    if (meal.type != Wj::kObj) continue;
    ++count;
    kcal += ChildNum(meal, "total_kcal");
    p += ChildNum(meal, "protein_g");
    c += ChildNum(meal, "carbs_g");
    f += ChildNum(meal, "fat_g");
    fiber += ChildNum(meal, "fiber_g");
  }
  Wj t;
  t.type = Wj::kObj;
  WjSet(t, "total_kcal", WjNum(Round1(kcal), 1));
  WjSet(t, "protein_g", WjNum(Round1(p), 1));
  WjSet(t, "carbs_g", WjNum(Round1(c), 1));
  WjSet(t, "fat_g", WjNum(Round1(f), 1));
  WjSet(t, "fiber_g", WjNum(Round1(fiber), 1));
  WjSet(t, "meal_count", WjInt(count));
  return t;
}

bool SaveDay(const std::wstring& nutri, const std::string& day, Wj dayDoc) {
  if (!EnsureDir(nutri) || !EnsureDir(Join(nutri, L"days"))) return false;
  WjSet(dayDoc, "date", WjStr(day));
  WjSet(dayDoc, "updated_at", WjStr(UtcNow()));
  const std::string body = WjStringify(dayDoc);
  if (!WriteAtomic(DayPath(nutri, day), body)) return false;
  if (day == LocalToday()) {
    const size_t slash = nutri.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
      WriteAtomic(Join(nutri.substr(0, slash), L"nutrition_today.json"), body);
    }
  }
  return true;
}

std::string WeekdayName() {
  static const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                "Thursday", "Friday", "Saturday"};
  SYSTEMTIME st;
  GetLocalTime(&st);
  if (st.wDayOfWeek > 6) return "";
  return kDays[st.wDayOfWeek];
}

Wj Stamp() {
  Wj s;
  s.type = Wj::kObj;
  const std::string clock = LocalClock();
  WjSet(s, "timestamp", WjStr(UtcNow()));
  WjSet(s, "local_time", WjStr(clock));
  WjSet(s, "local_clock", WjStr(clock));
  WjSet(s, "local_date", WjStr(LocalToday()));
  WjSet(s, "tz_offset_min", WjInt(TzOffsetMin()));
  WjSet(s, "weekday", WjStr(WeekdayName()));
  return s;
}

std::string Clip(std::string s, size_t n) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
  if (s.size() > n) s.resize(n);
  return s;
}

bool ChildInt(const Wj& obj, const char* key, long long* out) {
  const Wj* v = WjGet(obj, key);
  if (!v || v->type != Wj::kNum || v->num.empty()) return false;
  *out = strtoll(v->num.c_str(), nullptr, 10);
  return true;
}

void AbsorbMeta(Wj* into, const Wj* src) {
  if (!src || src->type != Wj::kObj) return;
  for (const auto& kv : src->obj) WjSet(*into, kv.first, kv.second);
}

void ApplyMealMeta(Wj* row, const Wj& meta) {
  static const char* kStr[] = {"client", "capture", "camera_label", "ua_hint",
                               "notes", "photo_description", "logged_via"};
  Wj cleaned;
  cleaned.type = Wj::kObj;
  for (const char* key : kStr) {
    std::string s = Clip(ChildStr(meta, key), 240);
    if (!s.empty()) WjSet(cleaned, key, WjStr(s));
  }
  Wj timings;
  timings.type = Wj::kObj;
  static const char* kMs[] = {"recognize_ms", "capture_ms", "prepare_ms", "log_ms", "total_ms", "image_bytes"};
  for (const char* key : kMs) {
    long long n = 0;
    if (ChildInt(meta, key, &n)) WjSet(timings, key, WjInt(n));
  }
  if (const Wj* names = WjGet(meta, "suggested_names")) {
    if (names->type == Wj::kArr) {
      Wj kept;
      kept.type = Wj::kArr;
      for (const Wj& item : names->arr) {
        if (kept.arr.size() >= 8) break;
        std::string s;
        if (!WjAsString(item, &s)) continue;
        s = Clip(s, 80);
        if (!s.empty()) kept.arr.push_back(WjStr(s));
      }
      if (!kept.arr.empty()) WjSet(cleaned, "suggested_names", std::move(kept));
    }
  }
  if (const Wj* conf = WjGet(meta, "photo_confidence")) {
    if (conf->type == Wj::kNum && !conf->num.empty()) {
      double c = strtod(conf->num.c_str(), nullptr);
      WjSet(cleaned, "photo_confidence", WjNum(std::round(c * 1000.0) / 1000.0, 3));
    }
  }
  std::string capture = ChildStr(cleaned, "capture");
  if (capture.empty()) capture = "manual";
  std::string client = ChildStr(cleaned, "client");
  if (client.empty()) client = "unknown";
  WjSet(*row, "capture", WjStr(capture));
  WjSet(*row, "client", WjStr(client));
  WjSet(*row, "location_tag", WjStr(client == "unknown" ? "focus" : client));
  WjSet(*row, "timings", std::move(timings));
  WjSet(*row, "meta", std::move(cleaned));
}

void LogEvent(const std::wstring& nutri, const char* kind, Wj extra) {
  if (!EnsureDir(nutri)) return;
  Wj row = Stamp();
  WjSet(row, "event", WjStr(kind));
  if (extra.type == Wj::kObj) {
    for (auto& kv : extra.obj) WjSet(row, kv.first, std::move(kv.second));
  }
  AppendEvent(Join(nutri, L"nutrition_events.jsonl"), WjStringify(row));
}

std::string NewMealId() {
  static volatile LONG seq = 0;
  return "m" + std::to_string(GetTickCount64()) + "_" + std::to_string(InterlockedIncrement(&seq));
}

NutriReply Reply(int code, const std::string& json) { return NutriReply{code, json}; }

NutriReply Today(const std::wstring& nutri) {
  const std::string day = LocalToday();
  Wj doc = LoadDay(DayPath(nutri, day), day);
  Wj meals = MealsOf(doc);
  Wj totals = TotalsOf(meals);
  Wj out;
  out.type = Wj::kObj;
  WjSet(out, "ok", WjBool(true));
  WjSet(out, "date", WjStr(day));
  WjSet(out, "meals", meals);
  WjSet(out, "totals", totals);
  return Reply(200, WjStringify(out));
}

NutriReply AddMeals(const std::wstring& nutri, const std::string& body) {
  Wj req;
  std::string err;
  if (!WjParse(body, &req, &err) || req.type != Wj::kObj) {
    return Reply(400, "{\"ok\":false,\"detail\":\"invalid JSON\"}");
  }
  const Wj* items = WjGet(req, "items");
  if (!items || items->type != Wj::kArr) {
    return Reply(400, "{\"ok\":false,\"detail\":\"items required\"}");
  }
  std::string mealType = ChildStr(req, "meal_type");
  if (mealType.empty()) mealType = "lunch";
  const std::string day = LocalToday();
  Wj doc = LoadDay(DayPath(nutri, day), day);
  Wj meals = MealsOf(doc);
  Wj added;
  added.type = Wj::kArr;
  for (const Wj& it : items->arr) {
    if (it.type != Wj::kObj) continue;
    std::string name = ChildStr(it, "food_name");
    if (name.empty()) continue;
    double weight = ChildNum(it, "weight_g");
    if (weight <= 0) weight = 100;
    double servings = ChildNum(it, "servings");
    if (servings <= 0) servings = 1;
    const PerG food = EstimatePerG(name);
    const bool known = std::string(food.source) != "fallback";
    Wj meta;
    meta.type = Wj::kObj;
    AbsorbMeta(&meta, WjGet(req, "meta"));
    AbsorbMeta(&meta, WjGet(it, "meta"));
    Wj row = Stamp();
    WjSet(row, "meal_id", WjStr(NewMealId()));
    WjSet(row, "food_item", WjStr(name));
    WjSet(row, "weight_g", WjNum(Round1(weight), 1));
    WjSet(row, "servings", WjNum(Round1(servings), 1));
    WjSet(row, "meal_type", WjStr(mealType));
    WjSet(row, "total_kcal", WjNum(Round1(weight * food.kcal), 1));
    WjSet(row, "protein_g", WjNum(Round1(weight * food.p), 1));
    WjSet(row, "carbs_g", WjNum(Round1(weight * food.c), 1));
    WjSet(row, "fat_g", WjNum(Round1(weight * food.f), 1));
    WjSet(row, "fiber_g", WjNum(Round1(weight * food.fiber), 1));
    WjSet(row, "macros_source", WjStr(food.source));
    WjSet(row, "confidence", WjNum(known ? 0.5 : 0.2, 1));
    WjSet(row, "source", WjStr("nutrinode"));
    ApplyMealMeta(&row, meta);
    Wj ev;
    ev.type = Wj::kObj;
    WjSet(ev, "meal_id", WjStr(ChildStr(row, "meal_id")));
    WjSet(ev, "food_item", WjStr(name));
    WjSet(ev, "weight_g", WjNum(Round1(weight), 1));
    WjSet(ev, "meal_type", WjStr(mealType));
    WjSet(ev, "total_kcal", WjNum(Round1(weight * food.kcal), 1));
    WjSet(ev, "macros_source", WjStr(food.source));
    WjSet(ev, "capture", WjStr(ChildStr(row, "capture")));
    WjSet(ev, "client", WjStr(ChildStr(row, "client")));
    if (const Wj* timings = WjGet(row, "timings")) WjSet(ev, "timings", *timings);
    LogEvent(nutri, "meal_logged", std::move(ev));
    meals.arr.insert(meals.arr.begin(), row);
    added.arr.push_back(std::move(row));
  }
  WjSet(doc, "meals", meals);
  Wj totals = TotalsOf(meals);
  WjSet(doc, "totals", totals);
  if (!SaveDay(nutri, day, doc)) return Reply(500, "{\"ok\":false,\"detail\":\"write_failed\"}");
  Wj out;
  out.type = Wj::kObj;
  WjSet(out, "ok", WjBool(true));
  WjSet(out, "meals", added);
  WjSet(out, "totals", totals);
  WjSet(out, "logged_at", Stamp());
  return Reply(200, WjStringify(out));
}

NutriReply DeleteMeal(const std::wstring& nutri, const std::string& mealId) {
  const std::string day = LocalToday();
  Wj doc = LoadDay(DayPath(nutri, day), day);
  Wj meals = MealsOf(doc);
  Wj kept;
  kept.type = Wj::kArr;
  bool found = false;
  for (const Wj& meal : meals.arr) {
    if (meal.type == Wj::kObj && ChildStr(meal, "meal_id") == mealId) {
      found = true;
      continue;
    }
    kept.arr.push_back(meal);
  }
  if (!found) return Reply(404, "{\"ok\":false,\"detail\":\"meal not found\"}");
  Wj ev;
  ev.type = Wj::kObj;
  WjSet(ev, "meal_id", WjStr(mealId));
  LogEvent(nutri, "meal_deleted", std::move(ev));
  WjSet(doc, "meals", kept);
  Wj totals = TotalsOf(kept);
  WjSet(doc, "totals", totals);
  if (!SaveDay(nutri, day, doc)) return Reply(500, "{\"ok\":false,\"detail\":\"write_failed\"}");
  Wj out;
  out.type = Wj::kObj;
  WjSet(out, "ok", WjBool(true));
  WjSet(out, "totals", totals);
  return Reply(200, WjStringify(out));
}

std::string GeminiKey(const std::wstring& nutri) {
  if (const char* e = std::getenv("GEMINI_API_KEY")) {
    if (e[0]) return e;
  }
  if (const char* e = std::getenv("LLM_CLOUD_API_KEY")) {
    if (e[0]) return e;
  }
  if (const char* e = std::getenv("LLM_API_KEY")) {
    if (e[0]) return e;
  }
  std::string raw;
  std::string err;
  Wj doc;
  if (ReadFileCap(Join(nutri, L"nutrition_llm.json"), &raw, 64 * 1024) && WjParse(raw, &doc, &err)) {
    std::string key = ChildStr(doc, "gemini_api_key");
    if (!key.empty()) return key;
  }
  return "";
}

std::wstring Wide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) return L"";
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

bool WinHttpPost(const std::wstring& host, const std::wstring& path, const std::wstring& headers,
                 const std::string& body, std::string* out) {
  out->clear();
  HINTERNET ses = WinHttpOpen(L"CALTFocus/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return false;
  HINTERNET con = WinHttpConnect(ses, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return false;
  }
  HINTERNET req = WinHttpOpenRequest(con, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  WinHttpSetTimeouts(req, 15000, 15000, 90000, 90000);
  BOOL ok = WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(),
                               (DWORD)body.size(), 0);
  if (ok) ok = WinHttpReceiveResponse(req, nullptr);
  if (ok) {
    for (;;) {
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
      std::string chunk(avail, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
      chunk.resize(read);
      out->append(chunk);
      if (out->size() > 2 * 1024 * 1024) break;
    }
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  return ok && !out->empty();
}

std::string StripB64(std::string s) {
  size_t comma = s.find(',');
  if (comma != std::string::npos && s.find("base64") != std::string::npos) s = s.substr(comma + 1);
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') o.push_back(c);
  }
  return o;
}

NutriReply AnalyzePhoto(const std::wstring& nutri, const std::string& body) {
  const std::string key = GeminiKey(nutri);
  if (key.empty()) {
    return Reply(503,
                 "{\"ok\":false,\"detail\":\"Photo AI needs GEMINI_API_KEY or LLM_CLOUD_API_KEY "
                 "(env, or behavior/nutrition/nutrition_llm.json).\"}");
  }
  Wj req;
  std::string err;
  if (!WjParse(body, &req, &err) || req.type != Wj::kObj) {
    return Reply(400, "{\"ok\":false,\"detail\":\"invalid JSON\"}");
  }
  std::string b64 = StripB64(ChildStr(req, "image_b64"));
  if (b64.empty()) b64 = StripB64(ChildStr(req, "image"));
  if (b64.size() < 32) return Reply(400, "{\"ok\":false,\"detail\":\"invalid image_b64\"}");
  std::string mime = ChildStr(req, "mime");
  if (mime != "image/png" && mime != "image/webp") mime = "image/jpeg";
  const ULONGLONG t0 = GetTickCount64();
  const char* prompt =
      "You are a nutrition assistant looking at a plate or food photo. "
      "Respond ONLY with JSON containing an items array of suggested_name, estimated_weight_g, "
      "and confidence, plus a description string. List 1 to 6 items. Prefer common Indian dish names.";
  std::string payload = std::string("{\"contents\":[{\"role\":\"user\",\"parts\":[") +
                         "{\"text\":\"" + prompt + "\"}," + "{\"inline_data\":{\"mime_type\":\"" + mime +
                         "\",\"data\":\"" + b64 + "\"}}]}]," +
                         "\"generationConfig\":{\"temperature\":0.2,\"maxOutputTokens\":1024}}";
  std::string model = "gemini-2.0-flash";
  if (const char* envModel = std::getenv("NUTRITION_VISION_MODEL")) {
    size_t n = 0;
    bool ok = true;
    for (const char* p = envModel; *p; ++p, ++n) {
      const unsigned char c = (unsigned char)*p;
      if (n >= 64 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == '.' || c == '-' || c == '_')) {
        ok = false;
        break;
      }
    }
    if (ok && n > 0) model = envModel;
  }
  std::wstring path = L"/v1beta/models/" + Wide(model) + L":generateContent";
  std::wstring headers = L"Content-Type: application/json\r\nx-goog-api-key: " + Wide(key) + L"\r\n";
  std::string raw;
  if (!WinHttpPost(L"generativelanguage.googleapis.com", path, headers, payload, &raw)) {
    return Reply(502, "{\"ok\":false,\"detail\":\"Photo analysis failed\"}");
  }
  Wj gem;
  if (!WjParse(raw, &gem, &err)) return Reply(502, "{\"ok\":false,\"detail\":\"Photo analysis failed\"}");
  std::string text;
  if (const Wj* cands = WjGet(gem, "candidates")) {
    if (cands->type == Wj::kArr && !cands->arr.empty() && cands->arr[0].type == Wj::kObj) {
      if (const Wj* content = WjGet(cands->arr[0], "content")) {
        if (const Wj* parts = WjGet(*content, "parts")) {
          if (parts->type == Wj::kArr) {
            for (const Wj& part : parts->arr) {
              std::string bit = ChildStr(part, "text");
              if (!bit.empty()) text += bit;
            }
          }
        }
      }
    }
  }
  size_t a = text.find('{');
  size_t b = text.rfind('}');
  Wj parsed;
  if (a == std::string::npos || b == std::string::npos || b < a ||
      !WjParse(text.substr(a, b - a + 1), &parsed, &err)) {
    return Reply(502, "{\"ok\":false,\"detail\":\"Photo analysis returned no foods\"}");
  }
  Wj out;
  out.type = Wj::kObj;
  WjSet(out, "ok", WjBool(true));
  if (const Wj* items = WjGet(parsed, "items"))
    WjSet(out, "items", *items);
  else {
    Wj empty;
    empty.type = Wj::kArr;
    WjSet(out, "items", empty);
  }
  std::string desc = ChildStr(parsed, "description");
  WjSet(out, "description", desc.empty() ? WjNull() : WjStr(desc));
  WjSet(out, "recognize_ms", WjInt((long long)(GetTickCount64() - t0)));
  WjSet(out, "image_bytes", WjInt((long long)(b64.size() * 3 / 4)));
  WjSet(out, "mime", WjStr(mime));
  WjSet(out, "source", WjStr("gemini"));
  Wj ev;
  ev.type = Wj::kObj;
  WjSet(ev, "item_count", WjInt((long long)(WjGet(out, "items") && WjGet(out, "items")->type == Wj::kArr
                                                 ? WjGet(out, "items")->arr.size()
                                                 : 0)));
  WjSet(ev, "description", desc.empty() ? WjNull() : WjStr(Clip(desc, 200)));
  WjSet(ev, "recognize_ms", WjInt((long long)(GetTickCount64() - t0)));
  WjSet(ev, "mime", WjStr(mime));
  WjSet(ev, "source", WjStr("gemini"));
  LogEvent(nutri, "photo_recognized", std::move(ev));
  return Reply(200, WjStringify(out));
}

std::string UrlDecode(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int hi = hex(s[i + 1]);
      int lo = hex(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        o.push_back((char)((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    o.push_back(s[i] == '+' ? ' ' : s[i]);
  }
  return o;
}

NutriReply Events(const std::wstring& nutri) {
  Wj rows;
  rows.type = Wj::kArr;
  std::string raw;
  if (ReadFileCap(Join(nutri, L"nutrition_events.jsonl"), &raw, 2 * 1024 * 1024)) {
    std::vector<std::string> lines;
    size_t i = 0;
    while (i < raw.size()) {
      size_t nl = raw.find('\n', i);
      if (nl == std::string::npos) nl = raw.size();
      std::string line = raw.substr(i, nl - i);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) lines.push_back(std::move(line));
      i = nl < raw.size() ? nl + 1 : raw.size();
    }
    size_t start = lines.size() > 40 ? lines.size() - 40 : 0;
    for (size_t n = start; n < lines.size(); ++n) {
      Wj row;
      std::string err;
      if (WjParse(lines[n], &row, &err)) rows.arr.push_back(std::move(row));
    }
  }
  Wj out;
  out.type = Wj::kObj;
  WjSet(out, "ok", WjBool(true));
  WjSet(out, "events", std::move(rows));
  return Reply(200, WjStringify(out));
}

}  // namespace

NutriReply EstimateRoute(const std::string& body) {
  Wj req;
  std::string err;
  if (!WjParse(body, &req, &err) || req.type != Wj::kObj) {
    return Reply(400, "{\"ok\":false,\"detail\":\"invalid JSON\"}");
  }
  std::string name = ChildStr(req, "food_name");
  if (name.empty()) name = ChildStr(req, "food_item");
  if (name.empty()) return Reply(400, "{\"ok\":false,\"detail\":\"food_name required\"}");
  double weight = ChildNum(req, "weight_g");
  if (weight <= 0) weight = 100;
  const PerG food = EstimatePerG(name);
  Wj out;
  out.type = Wj::kObj;
  WjSet(out, "ok", WjBool(true));
  WjSet(out, "food_name", WjStr(name));
  WjSet(out, "weight_g", WjNum(Round1(weight), 1));
  WjSet(out, "total_kcal", WjNum(Round1(weight * food.kcal), 1));
  WjSet(out, "protein_g", WjNum(Round1(weight * food.p), 1));
  WjSet(out, "carbs_g", WjNum(Round1(weight * food.c), 1));
  WjSet(out, "fat_g", WjNum(Round1(weight * food.f), 1));
  WjSet(out, "fiber_g", WjNum(Round1(weight * food.fiber), 1));
  WjSet(out, "macros_source", WjStr(food.source));
  return Reply(200, WjStringify(out));
}

NutriReply NutriHandle(const std::wstring& behaviorDir, const std::string& method,
                       const std::string& path, const std::string& body) {
  const std::wstring nutri = Join(behaviorDir, L"nutrition");
  std::string p = path;
  if (p.size() > 1 && p.back() == '/') p.pop_back();
  if (method == "GET" && (p == "/api/nutrition/today" || p == "/api/nutrition")) return Today(nutri);
  if (method == "GET" && p == "/api/nutrition/events") return Events(nutri);
  if (method == "POST" && p == "/api/nutrition/meals") return AddMeals(nutri, body);
  if (method == "POST" && p == "/api/nutrition/foods/estimate") return EstimateRoute(body);
  if (method == "POST" && p == "/api/nutrition/analyze-photo") return AnalyzePhoto(nutri, body);
  const std::string meals = "/api/nutrition/meals/";
  if (method == "DELETE" && p.compare(0, meals.size(), meals) == 0) {
    return DeleteMeal(nutri, UrlDecode(p.substr(meals.size())));
  }
  return Reply(404, "{\"ok\":false,\"detail\":\"unknown nutrition route\"}");
}
