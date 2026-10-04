#include "softland_publish.h"

#include "day_loop.h"
#include "life_content.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

}  // namespace

bool WriteBehaviorMirrorFile(const std::wstring& behaviorDir, const wchar_t* fileName,
                             const std::string& body) {
  if (behaviorDir.empty() || !fileName || !fileName[0] || body.empty()) return false;
  CreateDirectoryW(behaviorDir.c_str(), nullptr);
  const std::wstring dest = Join(behaviorDir, fileName);
  const std::wstring tmp = dest + L".tmp";

  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, tmp.c_str(), L"wb");
#else
  f = _wfopen(tmp.c_str(), L"wb");
#endif
  if (!f) return false;
  fwrite(body.data(), 1, body.size(), f);
  fclose(f);

  if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#if defined(_MSC_VER)
    _wfopen_s(&f, dest.c_str(), L"wb");
#else
    f = _wfopen(dest.c_str(), L"wb");
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

bool PublishSoftlandMirror(const std::wstring& behaviorDir, const ProductivitySoftland& s) {
  if (s.document_json.empty() || behaviorDir.empty()) return false;
  return WriteBehaviorMirrorFile(behaviorDir, L"softland_policy.json", s.document_json);
}

namespace {

std::string AddDaysYmd(const std::string& ymd, int deltaDays) {
  if (ymd.size() < 10) return ymd;
  SYSTEMTIME st{};
  st.wYear = (WORD)atoi(ymd.substr(0, 4).c_str());
  st.wMonth = (WORD)atoi(ymd.substr(5, 2).c_str());
  st.wDay = (WORD)atoi(ymd.substr(8, 2).c_str());
  FILETIME ft{};
  if (!SystemTimeToFileTime(&st, &ft)) return ymd;
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  const ULONGLONG day = 864000000000ULL;
  if (deltaDays >= 0)
    u.QuadPart += day * (ULONGLONG)deltaDays;
  else
    u.QuadPart -= day * (ULONGLONG)(-deltaDays);
  ft.dwLowDateTime = u.LowPart;
  ft.dwHighDateTime = u.HighPart;
  SYSTEMTIME out{};
  FileTimeToSystemTime(&ft, &out);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", out.wYear, out.wMonth, out.wDay);
  return buf;
}

}  // namespace

bool PublishDayStatusMirror(const std::wstring& behaviorDir) {
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return false;
  const int qualified = ProductivityRewardCount("qualified");
  const int used = ProductivityRewardCount("used");
  const int granted = ProductivityRewardGranted();
  const int earned = qualified / 4;
  int available = earned + granted - used;
  if (available < 0) available = 0;
  const int toNext = 4 - (qualified % 4);
  std::string freeJs = s.free_until.empty() ? "null" : ("\"" + s.free_until + "\"");
  std::string body =
      std::string("{") + "\"qualified_days\":" + std::to_string(qualified) +
      ",\"reward_earned\":" + std::to_string(earned) + ",\"reward_granted\":" +
      std::to_string(granted) + ",\"reward_spent\":" + std::to_string(used) +
      ",\"reward_available\":" + std::to_string(available) + ",\"days_to_next_reward\":" +
      std::to_string(toNext) + ",\"passes_limit\":2,\"passes_used\":" +
      std::to_string(ProductivityPassesUsedThisWeek()) +
      ",\"pass_today\":" + (ProductivityPassGrantedToday() ? "true" : "false") +
      ",\"earned_today_seconds\":" + std::to_string(ProductivityDayEarnedSecondsToday()) +
      ",\"balance_seconds\":" + std::to_string(s.earned_ledger_seconds) + ",\"free_until\":" +
      freeJs + ",\"reward_day_active\":" + (s.reward_day_active ? "true" : "false") + "}";
  return WriteBehaviorMirrorFile(behaviorDir, L"day_status.json", body);
}

bool PublishPlanBlocksMirror(const std::wstring& behaviorDir) {
  ProductivityEnsurePlanner();
  const std::string today = ProductivityLocalDate();
  const std::string from = AddDaysYmd(today, -7) + "T00:00:00";
  const std::string to = AddDaysYmd(today, 14) + "T00:00:00";
  std::string blocks = ProductivityPlanListJson(from, to, 1);
  std::string routines = ProductivityRoutineListJson(1);
  if (blocks.empty()) blocks = "[]";
  if (routines.empty()) routines = "[]";
  std::string body = std::string("{\"from\":\"") + from + "\",\"to\":\"" + to +
                     "\",\"blocks\":" + blocks + ",\"routines\":" + routines + "}";
  return WriteBehaviorMirrorFile(behaviorDir, L"plan_blocks.json", body);
}

void PublishFocusReadMirrors(const std::wstring& behaviorDir) {
  if (behaviorDir.empty()) return;
  PublishDayLoopMirror(behaviorDir);
  PublishDayStatusMirror(behaviorDir);
  PublishJournalMirrors(behaviorDir, 1);
  PublishPlanBlocksMirror(behaviorDir);
  std::wstring dataDir = behaviorDir;
  const size_t slash = dataDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
  PublishBibleDevotionMirror(behaviorDir, dataDir + L"\\bible", 1);
}
