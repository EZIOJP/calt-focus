#include "life_mirrors.h"

#include "softland_publish.h"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string LocalDateStr(int offsetDays = 0) {
  time_t now = time(nullptr);
  now += static_cast<time_t>(offsetDays) * static_cast<time_t>(86400);
  struct tm t {};
#if defined(_MSC_VER)
  localtime_s(&t, &now);
#else
  {
    struct tm* p = localtime(&now);
    if (p) t = *p;
  }
#endif
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return buf;
}

std::string NowIsoLocal() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay,
           st.wHour, st.wMinute, st.wSecond);
  return buf;
}

bool ExtractJsonString(const std::string& body, const char* key, std::string& out) {
  out.clear();
  if (body.empty() || !key) return false;
  const std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
  if (p >= body.size() || body[p] != '"') return false;
  ++p;
  size_t end = p;
  while (end < body.size() && body[end] != '"') {
    if (body[end] == '\\' && end + 1 < body.size()) end += 2;
    else ++end;
  }
  if (end >= body.size()) return false;
  out.assign(body, p, end - p);
  return true;
}

bool ExtractJsonNumber(const std::string& body, const char* key, double& out) {
  out = 0;
  if (body.empty() || !key) return false;
  const std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
  if (p >= body.size()) return false;
  if (body.compare(p, 4, "null") == 0) return false;
  char* end = nullptr;
  const double v = strtod(body.c_str() + p, &end);
  if (end == body.c_str() + p) return false;
  out = v;
  return true;
}

int ClampInt(double v, int lo, int hi) {
  int n = static_cast<int>(v >= 0 ? v + 0.5 : v - 0.5);
  if (n < lo) return lo;
  if (n > hi) return hi;
  return n;
}

std::string EmptyLifeJson(const std::string& day) {
  return std::string("{\"date\":\"") + day +
         "\",\"empty\":true,\"source\":\"calt_focus\","
         "\"sleep_hours\":0,\"sleep_quality\":3,\"exercise_minutes\":0,"
         "\"water_glasses\":0,\"meals_healthy\":0,\"study_minutes\":0,"
         "\"tasks_completed\":0,\"deep_work_blocks\":0,\"screen_time_hours\":0,"
         "\"social_media_minutes\":0,\"outdoor_minutes\":0,\"mood_score\":3,"
         "\"stress_level\":3,\"meditation_minutes\":0}";
}

/** Build life daily row from wearable_day.json summary fields. */
std::string LifeFromWearableSummary(const std::string& day, const std::string& wear,
                                    int studyMinutes) {
  double sleepHours = 0;
  double sleepScore = 0;
  double steps = 0;
  double stress = 0;
  double hr = 0;
  // Treat sleep_hours:0 / missing as no sleep — thin stubs must not wipe a richer mirror.
  const bool hasSleep = ExtractJsonNumber(wear, "sleep_hours", sleepHours) && sleepHours > 0;
  const bool hasScore = ExtractJsonNumber(wear, "sleep_score", sleepScore) && sleepScore > 0;
  const bool hasSteps = ExtractJsonNumber(wear, "steps", steps) && steps > 0;
  const bool hasStress = ExtractJsonNumber(wear, "stress", stress) && stress > 0;
  ExtractJsonNumber(wear, "hr_last", hr);

  std::string source;
  if (!ExtractJsonString(wear, "source", source) || source.empty()) source = "calt_zepp";
  std::string synced;
  if (!ExtractJsonString(wear, "synced_at", synced)) synced = NowIsoLocal();

  int sleepQuality = 3;
  if (hasScore) {
    if (sleepScore > 5) sleepQuality = ClampInt(sleepScore / 20.0, 1, 5);
    else sleepQuality = ClampInt(sleepScore, 1, 5);
  }
  int exercise = 0;
  if (hasSteps && steps > 0) exercise = ClampInt(steps / 100.0, 0, 180);
  int stressLevel = 3;
  if (hasStress && stress > 0) stressLevel = ClampInt(stress / 20.0, 1, 5);

  const bool empty = !hasSleep && !hasSteps && !hasStress;
  const std::string stepsJson =
      hasSteps ? std::to_string(ClampInt(steps, 0, 200000)) : std::string("null");
  const std::string hrJson = hr > 0 ? std::to_string(ClampInt(hr, 0, 250)) : std::string("null");
  char buf[768];
  snprintf(buf, sizeof(buf),
           "{\"date\":\"%s\",\"empty\":%s,\"source\":\"%s\","
           "\"sleep_hours\":%.2f,\"sleep_quality\":%d,\"exercise_minutes\":%d,"
           "\"water_glasses\":0,\"meals_healthy\":0,\"study_minutes\":%d,"
           "\"tasks_completed\":0,\"deep_work_blocks\":0,\"screen_time_hours\":0,"
           "\"social_media_minutes\":0,\"outdoor_minutes\":0,\"mood_score\":3,"
           "\"stress_level\":%d,\"meditation_minutes\":0,"
           "\"steps\":%s,\"hr_last\":%s,\"synced_at\":\"%s\","
           "\"maintained_by\":\"calt_enforcer\"}",
           day.c_str(), empty ? "true" : "false", source.c_str(),
           hasSleep ? sleepHours : 0.0, sleepQuality, exercise, studyMinutes, stressLevel,
           stepsJson.c_str(), hrJson.c_str(), synced.c_str());
  return buf;
}

int StudyMinutesFromRollup(const std::wstring& behaviorDir) {
  const std::string roll = ReadFileUtf8(Join(behaviorDir, L"day_rollup.json"));
  if (roll.empty()) return 0;
  double mins = 0;
  if (ExtractJsonNumber(roll, "productive_minutes", mins)) return ClampInt(mins, 0, 24 * 60);
  double secs = 0;
  if (ExtractJsonNumber(roll, "productive_seconds", secs)) return ClampInt(secs / 60.0, 0, 24 * 60);
  return 0;
}

bool WriteUtf8File(const std::wstring& path, const std::string& body) {
  const std::wstring tmp = path + L".tmp";
  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, tmp.c_str(), L"wb");
#else
  f = _wfopen(tmp.c_str(), L"wb");
#endif
  if (!f) return false;
  fwrite(body.data(), 1, body.size(), f);
  fclose(f);
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#if defined(_MSC_VER)
    _wfopen_s(&f, path.c_str(), L"wb");
#else
    f = _wfopen(path.c_str(), L"wb");
#endif
    if (!f) {
      DeleteFileW(tmp.c_str());
      return false;
    }
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    DeleteFileW(tmp.c_str());
  }
  return true;
}

}  // namespace

void PublishLifeMirrors(const std::wstring& behaviorDir) {
  if (behaviorDir.empty()) return;

  const std::wstring daysDir = Join(behaviorDir, L"life_days");
  CreateDirectoryW(behaviorDir.c_str(), nullptr);
  CreateDirectoryW(daysDir.c_str(), nullptr);
  CreateDirectoryW(Join(behaviorDir, L"wearable_days").c_str(), nullptr);

  const std::string today = LocalDateStr(0);
  const int studyMins = StudyMinutesFromRollup(behaviorDir);

  // Prefer the dump that actually has sleep/steps — wearable_days/{day} can be a
  // thin stub that would wipe life_today sleep if preferred blindly.
  const std::string wearDayFile = ReadFileUtf8(
      Join(Join(behaviorDir, L"wearable_days"), std::wstring(today.begin(), today.end()) + L".json"));
  const std::string wearMirror = ReadFileUtf8(Join(behaviorDir, L"wearable_day.json"));
  auto wearScore = [](const std::string& w) -> int {
    if (w.empty()) return -1;
    int s = 0;
    double v = 0;
    if (ExtractJsonNumber(w, "sleep_hours", v) && v > 0) s += 4;
    if (ExtractJsonNumber(w, "steps", v) && v > 0) s += 2;
    if (w.find("\"sleep\"") != std::string::npos) s += 1;
    return s;
  };
  std::string wearToday =
      wearScore(wearMirror) >= wearScore(wearDayFile) ? wearMirror : wearDayFile;
  if (wearToday.empty()) wearToday = !wearMirror.empty() ? wearMirror : wearDayFile;

  std::string lifeToday = ReadFileUtf8(Join(daysDir, std::wstring(today.begin(), today.end()) + L".json"));
  if (lifeToday.empty()) lifeToday = ReadFileUtf8(Join(behaviorDir, L"life_today.json"));

  if (!wearToday.empty()) {
    // Rebuild today's life from watch summary + day_rollup study minutes.
    lifeToday = LifeFromWearableSummary(today, wearToday, studyMins);
    WriteUtf8File(Join(daysDir, std::wstring(today.begin(), today.end()) + L".json"), lifeToday);
    WriteBehaviorMirrorFile(behaviorDir, L"life_today.json", lifeToday);
  } else if (!lifeToday.empty()) {
    WriteBehaviorMirrorFile(behaviorDir, L"life_today.json", lifeToday);
  } else {
    lifeToday = EmptyLifeJson(today);
    WriteUtf8File(Join(daysDir, std::wstring(today.begin(), today.end()) + L".json"), lifeToday);
    WriteBehaviorMirrorFile(behaviorDir, L"life_today.json", lifeToday);
  }

  // Pack last 14 calendar days for one FE fetch (no hub spam).
  std::ostringstream pack;
  pack << "{\"ok\":true,\"schema\":1,\"updated_at\":\"" << NowIsoLocal()
       << "\",\"maintained_by\":\"calt_enforcer\",\"days\":[";
  for (int i = 13; i >= 0; --i) {
    const std::string day = LocalDateStr(-i);
    std::string row = ReadFileUtf8(Join(daysDir, std::wstring(day.begin(), day.end()) + L".json"));
    if (row.empty() && day == today) row = lifeToday;
    if (row.empty()) {
      // Derive from wearable_days/{day}.json when life row missing.
      const std::string wear =
          ReadFileUtf8(Join(Join(behaviorDir, L"wearable_days"),
                            std::wstring(day.begin(), day.end()) + L".json"));
      if (!wear.empty()) {
        row = LifeFromWearableSummary(day, wear, day == today ? studyMins : 0);
        WriteUtf8File(Join(daysDir, std::wstring(day.begin(), day.end()) + L".json"), row);
      } else {
        row = EmptyLifeJson(day);
      }
    }
    if (i < 13) pack << ',';
    pack << row;
  }
  pack << "]}";
  WriteBehaviorMirrorFile(behaviorDir, L"life_history_14.json", pack.str());

  // sleep_windows.json — Zepp start_min/end_min are often absolute minutes from the
  // watch local_date midnight (can be >1440, e.g. 1817→06:17 next day). Never clamp
  // those into 0..1439 (that painted a fake 23:59→full-day bar).
  if (!wearToday.empty()) {
    double hours = 0;
    double totalMin = 0;
    double startMinWatch = -1;
    double endMinWatch = -1;
    const bool hasH = ExtractJsonNumber(wearToday, "sleep_hours", hours) && hours > 0;
    size_t sleepObj = wearToday.find("\"sleep\"");
    std::string sleepFrag;
    if (sleepObj != std::string::npos) {
      sleepFrag = wearToday.substr(sleepObj, 1200);
      ExtractJsonNumber(sleepFrag, "total_min", totalMin);
      ExtractJsonNumber(sleepFrag, "start_min", startMinWatch);
      ExtractJsonNumber(sleepFrag, "end_min", endMinWatch);
    }
    if (!hasH && totalMin > 0) hours = totalMin / 60.0;
    if (hours <= 0 && totalMin <= 0 && !(startMinWatch >= 0 && endMinWatch >= 0)) {
      WriteBehaviorMirrorFile(behaviorDir, L"sleep_windows.json",
                              "{\"schema_version\":1,\"windows\":[],\"updated_at\":null}");
    } else {
      auto addMinsToYmd = [](const std::string& ymd, int absMin, char* out, size_t outN) {
        if (ymd.size() < 10 || outN < 20) {
          if (outN) out[0] = 0;
          return;
        }
        struct tm t {};
        t.tm_year = atoi(ymd.c_str()) - 1900;
        t.tm_mon = atoi(ymd.c_str() + 5) - 1;
        t.tm_mday = atoi(ymd.c_str() + 8);
        t.tm_isdst = -1;
        time_t tt = mktime(&t);
        if (tt == (time_t)-1) {
          out[0] = 0;
          return;
        }
        tt += static_cast<time_t>(absMin) * 60;
#if defined(_MSC_VER)
        localtime_s(&t, &tt);
#else
        {
          struct tm* p = localtime(&tt);
          if (p) t = *p;
          else {
            out[0] = 0;
            return;
          }
        }
#endif
        snprintf(out, outN, "%04d-%02d-%02dT%02d:%02d:00", t.tm_year + 1900, t.tm_mon + 1,
                 t.tm_mday, t.tm_hour, t.tm_min);
      };

      std::string baseDay = today;
      std::string watchDay;
      if (ExtractJsonString(wearToday, "local_date", watchDay) && watchDay.size() >= 10)
        baseDay = watchDay.substr(0, 10);
      else if (ExtractJsonString(wearToday, "watch_local_date", watchDay) && watchDay.size() >= 10)
        baseDay = watchDay.substr(0, 10);

      char startIso[40] = {};
      char endIso[40] = {};
      int mins = 0;
      std::string windowDate = today;

      if (startMinWatch >= 0 && endMinWatch >= 0 && endMinWatch != startMinWatch) {
        const int sAbs = (int)startMinWatch;
        const int eAbs = (int)endMinWatch;
        if (sAbs >= 1440 || eAbs >= 1440) {
          // Absolute minutes from watch local_date midnight (Zepp / Amazfit).
          addMinsToYmd(baseDay, sAbs, startIso, sizeof(startIso));
          addMinsToYmd(baseDay, eAbs, endIso, sizeof(endIso));
          mins = eAbs - sAbs;
          if (mins <= 0 && totalMin > 0) mins = ClampInt(totalMin, 1, 16 * 60);
        } else if (eAbs <= sAbs) {
          // Minutes-of-day overnight: previous evening → morning of baseDay.
          addMinsToYmd(baseDay, -(24 * 60 - sAbs), startIso, sizeof(startIso));
          addMinsToYmd(baseDay, eAbs, endIso, sizeof(endIso));
          mins = (24 * 60 - sAbs) + eAbs;
        } else {
          addMinsToYmd(baseDay, sAbs, startIso, sizeof(startIso));
          addMinsToYmd(baseDay, eAbs, endIso, sizeof(endIso));
          mins = eAbs - sAbs;
        }
        if (strlen(endIso) >= 10) windowDate = std::string(endIso, 10);
      } else {
        mins = totalMin > 0 ? ClampInt(totalMin, 1, 16 * 60)
                            : ClampInt(hours * 60.0, 1, 16 * 60);
        // Duration-only: bedtime-anchored overnight (23:00 → +duration).
        const int startMin = 23 * 60;
        addMinsToYmd(today, -(24 * 60 - startMin), startIso, sizeof(startIso));
        addMinsToYmd(today, -(24 * 60 - startMin) + mins, endIso, sizeof(endIso));
        if (strlen(endIso) >= 10) windowDate = std::string(endIso, 10);
      }
      if (mins <= 0) mins = ClampInt(hours * 60.0, 1, 16 * 60);
      if (mins > 16 * 60) mins = 16 * 60;
      if (!startIso[0] || !endIso[0]) {
        WriteBehaviorMirrorFile(behaviorDir, L"sleep_windows.json",
                                "{\"schema_version\":1,\"windows\":[],\"updated_at\":null}");
      } else {
        std::ostringstream sw;
        sw << "{\"schema_version\":1,\"updated_at\":\"" << NowIsoLocal()
           << "\",\"source\":\"calt_zepp\",\"windows\":[{\"date\":\"" << windowDate
           << "\",\"start\":\"" << startIso << "\",\"end\":\"" << endIso
           << "\",\"minutes\":" << mins << "}]}";
        WriteBehaviorMirrorFile(behaviorDir, L"sleep_windows.json", sw.str());
      }
    }
  }
}
