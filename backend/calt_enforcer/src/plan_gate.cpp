#include "plan_gate.h"

#include "productivity_store.h"
#include "softland_publish.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <string>

namespace {

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

std::string NormIso(std::string s) {
  if (s.empty() || s == "null") return {};
  if (!s.empty() && (s.back() == 'Z' || s.back() == 'z')) s.pop_back();
  size_t plus = s.find('+');
  if (plus != std::string::npos && plus > 10) s = s.substr(0, plus);
  if (s.size() > 19) s = s.substr(0, 19);
  return s;
}

/** Lexicographic on YYYY-MM-DDTHH:MM:SS — true if a is strictly before b. */
bool IsoBefore(const std::string& a, const std::string& b) {
  std::string na = NormIso(a);
  std::string nb = NormIso(b);
  if (na.empty()) return !nb.empty();
  if (nb.empty()) return false;
  return na < nb;
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

bool Contains(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}

/** Map category/title → free | study | none (routine/food: no SoftLand mutate). */
std::string GateModeForBlock(const std::string& category, const std::string& title) {
  const std::string c = Lower(category);
  const std::string t = Lower(title);
  if (c == "spiritual" || Contains(t, "bible") || Contains(t, "prayer") || Contains(t, "proverbs") ||
      Contains(t, "psalm"))
    return "study";
  if (c == "study" || c == "coursework" || c == "work" || Contains(t, "scaler")) return "study";
  if (c == "break" || c == "free" || c == "reward" || Contains(t, "free time") ||
      Contains(t, "freetime") || c == "leisure" || c == "rest" || c == "downtime")
    return "free";
  if (c == "food" || Contains(t, "breakfast") || Contains(t, "lunch") || Contains(t, "dinner") ||
      Contains(t, "meal"))
    return "none";
  if (Contains(t, "sleep") || Contains(t, "get ready to sleep")) return "study";
  return "none";
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(static_cast<char>(c));
    } else if (c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      o += buf;
    } else {
      o.push_back(static_cast<char>(c));
    }
  }
  return o;
}

bool ExtractObject(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && isspace(static_cast<unsigned char>(body[i]))) ++i;
  if (i >= body.size() || body[i] != '{') return false;
  int depth = 0;
  bool inStr = false;
  for (size_t j = i; j < body.size(); ++j) {
    char c = body[j];
    if (inStr) {
      if (c == '\\' && j + 1 < body.size()) {
        ++j;
        continue;
      }
      if (c == '"') inStr = false;
      continue;
    }
    if (c == '"') {
      inStr = true;
      continue;
    }
    if (c == '{') ++depth;
    else if (c == '}') {
      --depth;
      if (depth == 0) {
        *out = body.substr(i, j - i + 1);
        return true;
      }
    }
  }
  return false;
}

bool GetStringInObj(const std::string& obj, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = obj.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = obj.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < obj.size() && isspace(static_cast<unsigned char>(obj[i]))) ++i;
  if (i >= obj.size() || obj[i] != '"') return false;
  ++i;
  std::string v;
  while (i < obj.size()) {
    char c = obj[i++];
    if (c == '\\' && i < obj.size()) {
      v.push_back(obj[i++]);
      continue;
    }
    if (c == '"') break;
    v.push_back(c);
  }
  *out = v;
  return true;
}

struct ActiveBlock {
  long long id = 0;
  std::string title;
  std::string category;
  std::string start_at;
  std::string end_at;
  std::string status;
};

bool FindActiveBlock(int userId, const std::string& nowIso, ActiveBlock* out) {
  // Reuse plan list overlap: start < now+ε and end > now — query via list JSON for one window.
  // Narrow window: [now, now+1s) overlap uses store list with from=now, to=now+1s is wrong.
  // List uses start_at < to AND end_at > from. So from=now, to=now+1s → start < now+1s AND end > now.
  std::string from = nowIso;
  std::string to = nowIso;
  if (to.size() >= 19) {
    // bump one second crudely for upper bound
    to = nowIso.substr(0, 17) + "59";  // same minute ceiling — enough for overlap
  }
  // Better: from = now with end > now and start <= now via ProductivityPlanListJson day span.
  // Use today's midnight → tomorrow for list then pick active in C++.
  SYSTEMTIME st;
  GetLocalTime(&st);
  char dayFrom[40], dayTo[40];
  snprintf(dayFrom, sizeof(dayFrom), "%04u-%02u-%02uT00:00:00", st.wYear, st.wMonth, st.wDay);
  // next day
  FILETIME ft;
  SystemTimeToFileTime(&st, &ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  u.QuadPart += 24ULL * 60 * 60 * 10000000ULL;
  ft.dwLowDateTime = u.LowPart;
  ft.dwHighDateTime = u.HighPart;
  SYSTEMTIME st2;
  FileTimeToSystemTime(&ft, &st2);
  snprintf(dayTo, sizeof(dayTo), "%04u-%02u-%02uT00:00:00", st2.wYear, st2.wMonth, st2.wDay);

  std::string arr = ProductivityPlanListJson(dayFrom, dayTo, userId);
  // Parse objects crudely — find first with start_at <= now < end_at and status ok.
  const std::string nowN = NormIso(nowIso);
  size_t i = 0;
  while (i < arr.size()) {
    size_t objStart = arr.find('{', i);
    if (objStart == std::string::npos) break;
    int depth = 0;
    bool inStr = false;
    size_t j = objStart;
    for (; j < arr.size(); ++j) {
      char c = arr[j];
      if (inStr) {
        if (c == '\\' && j + 1 < arr.size()) {
          ++j;
          continue;
        }
        if (c == '"') inStr = false;
        continue;
      }
      if (c == '"') {
        inStr = true;
        continue;
      }
      if (c == '{') ++depth;
      else if (c == '}') {
        --depth;
        if (depth == 0) {
          ++j;
          break;
        }
      }
    }
    std::string obj = arr.substr(objStart, j - objStart);
    i = j;

    std::string status;
    GetStringInObj(obj, "status", &status);
    status = Lower(status);
    if (status == "done" || status == "cancelled" || status == "rolled") continue;

    std::string startAt, endAt, title, category;
    GetStringInObj(obj, "start_at", &startAt);
    GetStringInObj(obj, "end_at", &endAt);
    GetStringInObj(obj, "title", &title);
    GetStringInObj(obj, "category", &category);
    std::string sn = NormIso(startAt);
    std::string en = NormIso(endAt);
    if (sn.empty() || en.empty()) continue;
    // start <= now < end
    if (sn <= nowN && nowN < en) {
      long long id = 0;
      size_t idp = obj.find("\"id\"");
      if (idp != std::string::npos) {
        size_t colon = obj.find(':', idp);
        if (colon != std::string::npos) {
          size_t k = colon + 1;
          while (k < obj.size() && isspace(static_cast<unsigned char>(obj[k]))) ++k;
          while (k < obj.size() && isdigit(static_cast<unsigned char>(obj[k]))) {
            id = id * 10 + (obj[k] - '0');
            ++k;
          }
        }
      }
      out->id = id;
      out->title = title;
      out->category = category;
      out->start_at = startAt;
      out->end_at = endAt;
      out->status = status;
      return true;
    }
  }
  return false;
}

bool SetRuntimePlanBlock(ProductivitySoftland& s, const std::string& planBlockJson) {
  std::string runtime;
  if (!ExtractObject(s.document_json, "runtime", &runtime)) return false;
  // Insert or replace plan_block inside runtime
  if (runtime.find("\"plan_block\"") != std::string::npos) {
    if (!ProductivityReplaceJsonValue(runtime, "plan_block", planBlockJson)) return false;
  } else {
    // inject before closing }
    size_t close = runtime.rfind('}');
    if (close == std::string::npos) return false;
    std::string inject = ",\"plan_block\":" + planBlockJson;
    // if runtime is "{}" no leading comma needed carefully
    bool emptyObj = true;
    for (size_t k = 1; k < close; ++k) {
      if (!isspace(static_cast<unsigned char>(runtime[k]))) {
        emptyObj = false;
        break;
      }
    }
    if (emptyObj)
      runtime = "{\"plan_block\":" + planBlockJson + "}";
    else
      runtime.insert(close, inject);
  }
  return ProductivityReplaceJsonValue(s.document_json, "runtime", runtime);
}

}  // namespace

bool ApplyActivePlanToSoftland(const std::wstring& behaviorDir, int userId) {
  if (userId <= 0) userId = 1;
  ProductivityEnsurePlanner();

  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return false;

  const std::string now = IsoLocalNow();
  ActiveBlock block;
  const bool has = FindActiveBlock(userId, now, &block);

  std::string prevPlan;
  {
    std::string runtime;
    if (ExtractObject(s.document_json, "runtime", &runtime)) {
      std::string pb;
      if (ExtractObject(runtime, "plan_block", &pb)) prevPlan = pb;
    }
  }

  bool changed = false;

  if (!has) {
    // Clear plan_block; if free_until was plan-sourced (matches prior until), clear it.
    if (!prevPlan.empty() && prevPlan != "null") {
      std::string prevUntil, prevMode;
      GetStringInObj(prevPlan, "until", &prevUntil);
      GetStringInObj(prevPlan, "mode", &prevMode);
      if (prevMode == "free" && !prevUntil.empty() && NormIso(s.free_until) == NormIso(prevUntil)) {
        s.free_until.clear();
        changed = true;
      }
      if (SetRuntimePlanBlock(s, "null")) changed = true;
    }
  } else {
    const std::string mode = GateModeForBlock(block.category, block.title);
    const std::string until = NormIso(block.end_at).empty() ? block.end_at : NormIso(block.end_at);
    // Prefer full end_at string as stored if already ISO-ish
    std::string untilStore = block.end_at;
    if (untilStore.size() > 19) untilStore = NormIso(untilStore);

    std::string planJson =
        std::string("{\"id\":") + std::to_string(block.id) + ",\"mode\":\"" + mode +
        "\",\"title\":\"" + JsonEscape(block.title) + "\",\"category\":\"" +
        JsonEscape(block.category) + "\",\"until\":\"" + JsonEscape(untilStore) +
        "\",\"applied_at\":\"" + now + "\"}";

    if (mode == "free") {
      // Extend free_until to block end; never shorten an already-longer window.
      if (s.free_until.empty() || IsoBefore(s.free_until, untilStore)) {
        s.free_until = untilStore;
        changed = true;
      }
    } else if (mode == "study") {
      // Tighten: clear plan-driven free window (or free_until that ends at prior plan until).
      std::string prevUntil, prevMode;
      if (!prevPlan.empty()) {
        GetStringInObj(prevPlan, "until", &prevUntil);
        GetStringInObj(prevPlan, "mode", &prevMode);
      }
      if (!s.free_until.empty()) {
        if (prevMode == "free" || NormIso(s.free_until) == NormIso(untilStore) ||
            NormIso(s.free_until) == NormIso(prevUntil)) {
          // Clear only if free ends at/before this study block end (keep longer spend).
          if (!IsoBefore(untilStore, s.free_until)) {
            s.free_until.clear();
            changed = true;
          }
        }
      }
      // Study is SoftLand default when not free — decide already study without free_until.
    }
    // mode == none: mirror only

    if (SetRuntimePlanBlock(s, planJson)) changed = true;
  }

  if (!changed) return false;

  s.updated_at = now;
  ProductivityApplyCacheToDocument(s);
  if (!ProductivitySaveSoftland(s)) return false;
  ProductivityBumpSeq();
  PublishSoftlandMirror(behaviorDir, s);
  return true;
}
