#include "softland_tick.h"
#include "cmd_gateway.h"
#include "day_loop.h"
#include "plan_gate.h"
#include "productivity_store.h"
#include "softland_publish.h"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

// ISO stamps are compared as YYYY-MM-DDTHH:MM:SS wall-clock prefixes: Python
// writers add an offset, the enforcer does not, and both mean local time.

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

std::string NormalizeIsoForCompare(std::string s) {
  // Strip timezone suffix for crude compare; empty = inactive
  if (s.empty() || s == "null") return {};
  if (!s.empty() && (s.back() == 'Z' || s.back() == 'z')) s.pop_back();
  size_t plus = s.find('+');
  if (plus != std::string::npos && plus > 10) s = s.substr(0, plus);
  if (s.size() > 19) s = s.substr(0, 19);
  return s;
}

bool IsoExpired(const std::string& until_iso, const std::string& now_iso) {
  std::string u = NormalizeIsoForCompare(until_iso);
  if (u.empty()) return false;
  return u <= now_iso;
}

}  // namespace

bool TickSoftlandClocks(const std::wstring& behaviorDir) {
  const std::string now = IsoLocalNow();
  bool changed = false;

  // 1. Out-of-band writers (Python reward day, legacy Settings) edit the mirror;
  //    pull those in before deciding anything from the SoT.
  if (ProductivityImportIfStale(behaviorDir + L"\\softland_policy.json")) changed = true;

  // 2. Delayed edits (akrasia horizon) become real here, not at request time.
  std::vector<ProductivityPendingRow> due;
  if (ProductivityDuePending(now, due)) {
    for (const auto& row : due) {
      std::string err = GatewayApplyOp(row.op, row.payload_json, behaviorDir);
      ProductivityMarkPending(row.id, err.empty() ? "applied" : "failed");
      if (err.empty()) changed = true;
    }
  }

  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return changed;

  bool expired = false;
  if (IsoExpired(s.incubation_until, now)) {
    s.incubation_until.clear();
    expired = true;
  }
  if (IsoExpired(s.free_until, now)) {
    s.free_until.clear();
    s.reward_day_active = false;
    expired = true;
  }

  if (expired) {
    s.updated_at = now;
    ProductivityApplyCacheToDocument(s);
    if (!ProductivitySaveSoftland(s)) return changed;
    ProductivityBumpSeq();
    PublishSoftlandMirror(behaviorDir, s);
    changed = true;
  }

  return changed;
}

bool TickSoftlandPlanAndDayLoop(const std::wstring& behaviorDir) {
  bool changed = false;
  // Phase 6b: active planner block → SoftLand free_until / runtime.plan_block
  if (ApplyActivePlanToSoftland(behaviorDir, 1)) changed = true;

  // Day loop: bedtime_active + emergency expiry + opportunistic close-out free
  {
    std::wstring dataDir = behaviorDir;
    size_t slash = dataDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
    std::wstring dbPath = dataDir + L"\\productivity.db";
    if (DayLoopTick(behaviorDir, dbPath)) changed = true;
  }
  return changed;
}
