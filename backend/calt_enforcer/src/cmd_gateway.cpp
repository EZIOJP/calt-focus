#include "cmd_gateway.h"
#include "day_loop.h"
#include "device_block.h"
#include "life_content.h"
#include "plan_gate.h"
#include "policy_db.h"
#include "productivity_store.h"
#include "softland_publish.h"

#include <windows.h>
#include <sddl.h>

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace {

HANDLE gPipe = INVALID_HANDLE_VALUE;
const wchar_t* kPipeName = L"\\\\.\\pipe\\calt_enforcer_cmd";

std::string IsoLocalNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

/** Decode one JSON string escape (\n, \t, \uXXXX, …). Advances i past the sequence. */
bool DecodeJsonEscape(const std::string& body, size_t& i, std::string* out) {
  if (i >= body.size() || body[i] != '\\' || i + 1 >= body.size()) return false;
  char e = body[++i];
  ++i;
  switch (e) {
    case '"':
    case '\\':
    case '/':
      out->push_back(e);
      return true;
    case 'b':
      out->push_back('\b');
      return true;
    case 'f':
      out->push_back('\f');
      return true;
    case 'n':
      out->push_back('\n');
      return true;
    case 'r':
      out->push_back('\r');
      return true;
    case 't':
      out->push_back('\t');
      return true;
    case 'u': {
      if (i + 4 > body.size()) return false;
      unsigned code = 0;
      for (int n = 0; n < 4; ++n) {
        char c = body[i++];
        code <<= 4;
        if (c >= '0' && c <= '9')
          code |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
          code |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
          code |= (unsigned)(c - 'A' + 10);
        else
          return false;
      }
      // BMP only — enough for notes/emoji astral via surrogate pairs later if needed.
      if (code < 0x80) {
        out->push_back((char)code);
      } else if (code < 0x800) {
        out->push_back((char)(0xC0 | (code >> 6)));
        out->push_back((char)(0x80 | (code & 0x3F)));
      } else {
        out->push_back((char)(0xE0 | (code >> 12)));
        out->push_back((char)(0x80 | ((code >> 6) & 0x3F)));
        out->push_back((char)(0x80 | (code & 0x3F)));
      }
      return true;
    }
    default:
      out->push_back(e);
      return true;
  }
}

bool JsonGetString(const std::string& body, const char* key, std::string* out) {
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
    if (body[i] == '\\') {
      if (!DecodeJsonEscape(body, i, &val)) return false;
      continue;
    }
    val.push_back(body[i++]);
  }
  *out = val;
  return true;
}

bool LockModeIsStrong(const std::string& mode) {
  return mode == "password" || mode == "phrase";
}

/** Unlock against *persisted* credentials (never against payload-mutated lock_mode). */
bool RequirePersistedUnlock(const EnforcerSnapshot& prev, const std::string& provided) {
  if (!LockModeIsStrong(prev.lock_mode)) return true;
  const std::string& expect =
      prev.lock_mode == "password" ? prev.unlock_password : prev.unlock_phrase;
  // Fail closed: password/phrase mode with empty secret must not disarm.
  if (expect.empty()) return false;
  return provided == expect;
}

bool JsonGetBool(const std::string& body, const char* key, bool* out) {
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

bool JsonGetInt(const std::string& body, const char* key, int* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size()) return false;
  *out = atoi(body.c_str() + i);
  return true;
}

std::string ExtractPayload(const std::string& body) {
  std::string needle = "\"payload\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return "{}";
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return "{}";
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size() || body[i] != '{') return "{}";
  int depth = 0;
  size_t start = i;
  for (; i < body.size(); ++i) {
    if (body[i] == '{')
      ++depth;
    else if (body[i] == '}') {
      --depth;
      if (depth == 0) return body.substr(start, i - start + 1);
    }
  }
  return "{}";
}

/** Raw JSON text of payload[key] — objects, arrays and scalars alike. */
bool JsonGetRaw(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && isspace((unsigned char)body[i])) ++i;
  if (i >= body.size()) return false;
  size_t end = i;
  if (body[i] == '{' || body[i] == '[') {
    char open = body[i];
    char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool inStr = false;
    for (end = i; end < body.size(); ++end) {
      char c = body[end];
      if (inStr) {
        if (c == '\\')
          ++end;
        else if (c == '"')
          inStr = false;
        continue;
      }
      if (c == '"')
        inStr = true;
      else if (c == open)
        ++depth;
      else if (c == close && --depth == 0) {
        ++end;
        break;
      }
    }
    if (depth != 0) return false;
  } else if (body[i] == '"') {
    end = i + 1;
    while (end < body.size() && body[end] != '"') {
      if (body[end] == '\\' && end + 1 < body.size())
        end += 2;
      else
        ++end;
    }
    if (end < body.size()) ++end;
  } else {
    while (end < body.size() && body[end] != ',' && body[end] != '}' && body[end] != ']') ++end;
    while (end > i && isspace((unsigned char)body[end - 1])) --end;
  }
  *out = body.substr(i, end - i);
  return !out->empty();
}

/** Rebuild a host array from payload so only sanitized strings reach the doc. */
bool SanitizedHostArray(const std::string& raw, std::string* out) {
  if (raw.empty() || raw[0] != '[') return false;
  std::string arr = "[";
  size_t i = 0;
  bool first = true;
  while (i < raw.size()) {
    if (raw[i] == '"') {
      std::string tok;
      size_t j = i + 1;
      while (j < raw.size() && raw[j] != '"') {
        if (raw[j] == '\\' && j + 1 < raw.size()) {
          tok.push_back(raw[j + 1]);
          j += 2;
          continue;
        }
        tok.push_back(raw[j++]);
      }
      i = (j < raw.size()) ? j + 1 : j;
      std::string host;
      for (char c : tok) {
        unsigned char u = (unsigned char)c;
        if (isalnum(u) || c == '.' || c == '-' || c == '_') host.push_back((char)tolower(u));
      }
      if (host.size() < 3 || host.find('.') == std::string::npos) continue;
      if (!first) arr += ",";
      first = false;
      arr += "\"" + host + "\"";
      continue;
    }
    ++i;
  }
  arr += "]";
  *out = arr;
  return true;
}

/** Reject raw JSON we would not want spliced into the policy document. */
bool RawJsonAcceptable(const std::string& raw, char expectOpen, size_t maxLen) {
  if (raw.empty() || raw[0] != expectOpen || raw.size() > maxLen) return false;
  for (char c : raw) {
    if ((unsigned char)c < 0x20) return false;
  }
  return true;
}

/** Parse YYYY-MM-DDTHH:MM:SS (any trailing offset ignored) as local time. */
bool ParseIsoLocal(const std::string& iso, SYSTEMTIME* out) {
  if (iso.size() < 19) return false;
  unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  if (sscanf(iso.c_str(), "%4u-%2u-%2uT%2u:%2u:%2u", &y, &mo, &d, &h, &mi, &se) != 6) return false;
  ZeroMemory(out, sizeof(*out));
  out->wYear = (WORD)y;
  out->wMonth = (WORD)mo;
  out->wDay = (WORD)d;
  out->wHour = (WORD)h;
  out->wMinute = (WORD)mi;
  out->wSecond = (WORD)se;
  return true;
}

std::string AddMinutesLocalIso(int minutes) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  FILETIME ft;
  SystemTimeToFileTime(&st, &ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  // Support negatives (incubation rate-limit lookback). Clamp at epoch.
  LONGLONG delta = (LONGLONG)minutes * 60LL * 10000000LL;
  if (delta < 0 && (ULONGLONG)(-delta) > uli.QuadPart)
    uli.QuadPart = 0;
  else
    uli.QuadPart = (ULONGLONG)((LONGLONG)uli.QuadPart + delta);
  ft.dwLowDateTime = uli.LowPart;
  ft.dwHighDateTime = uli.HighPart;
  SYSTEMTIME outSt;
  FileTimeToSystemTime(&ft, &outSt);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", outSt.wYear, outSt.wMonth, outSt.wDay,
           outSt.wHour, outSt.wMinute, outSt.wSecond);
  return buf;
}

bool ConfirmMatches(const std::string& payload, const char* expected) {
  std::string got;
  if (!JsonGetString(payload, "confirm", &got)) return false;
  size_t a = got.find_first_not_of(" \t\r\n");
  size_t b = got.find_last_not_of(" \t\r\n");
  if (a == std::string::npos) return false;
  std::string trimmed = got.substr(a, b - a + 1);
  for (auto& c : trimmed) c = (char)toupper((unsigned char)c);
  return trimmed == expected;
}

std::string EndOfLocalDayIso() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT23:59:59", st.wYear, st.wMonth, st.wDay);
  return buf;
}

/**
 * Stack a spend on top of a free window that is still running — otherwise
 * spending 10m during a reward day would cut the day short and burn the ledger.
 */
std::string ExtendFreeWindowIso(const std::string& current, int minutes) {
  SYSTEMTIME base;
  if (!ParseIsoLocal(current, &base)) return AddMinutesLocalIso(minutes);
  FILETIME baseFt, nowFt;
  SYSTEMTIME nowSt;
  GetLocalTime(&nowSt);
  if (!SystemTimeToFileTime(&base, &baseFt) || !SystemTimeToFileTime(&nowSt, &nowFt)) {
    return AddMinutesLocalIso(minutes);
  }
  if (CompareFileTime(&baseFt, &nowFt) <= 0) return AddMinutesLocalIso(minutes);

  ULARGE_INTEGER uli;
  uli.LowPart = baseFt.dwLowDateTime;
  uli.HighPart = baseFt.dwHighDateTime;
  uli.QuadPart += (ULONGLONG)minutes * 60ULL * 10000000ULL;
  baseFt.dwLowDateTime = uli.LowPart;
  baseFt.dwHighDateTime = uli.HighPart;
  SYSTEMTIME outSt;
  FileTimeToSystemTime(&baseFt, &outSt);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", outSt.wYear, outSt.wMonth, outSt.wDay,
           outSt.wHour, outSt.wMinute, outSt.wSecond);
  return buf;
}

std::string SaveAndPublish(ProductivitySoftland& s, const std::wstring& behaviorDir) {
  s.updated_at = IsoLocalNow();
  ProductivityApplyCacheToDocument(s);
  if (!ProductivitySaveSoftland(s)) return "store_save_failed";
  ProductivityBumpSeq();
  PublishSoftlandMirror(behaviorDir, s);
  // FE reads mirrors — refresh immediately after SoftLand writes (don't wait 1 min).
  PublishDayStatusMirror(behaviorDir);
  PublishDayLoopMirror(behaviorDir);
  return "";
}

std::string HandleOp(const std::string& op, const std::string& payload,
                     const std::wstring& behaviorDir, std::string* extraOut) {
  // A Python writer may have touched the mirror since the last tick; import
  // before read-modify-write so its change is not clobbered by this command.
  ProductivityImportIfStale(behaviorDir + L"\\softland_policy.json");

  if (op == "status.snapshot" || op.empty()) return "";

  if (op == "day.confirm_plan") {
    return DayLoopConfirmPlan(behaviorDir, extraOut);
  }
  if (op == "day.loop_snapshot") {
    std::wstring dataDir = behaviorDir;
    size_t slash = dataDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
    std::wstring dbPath = dataDir + L"\\productivity.db";
    return DayLoopSnapshot(behaviorDir, dbPath, extraOut);
  }
  if (op == "day.evaluate_close") {
    std::wstring dataDir = behaviorDir;
    size_t slash = dataDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
    std::wstring dbPath = dataDir + L"\\productivity.db";
    return DayLoopEvaluateClose(behaviorDir, dbPath, extraOut);
  }
  if (op == "day.task_list") {
    DayLoopEnsureSchema();
    std::string date;
    JsonGetString(payload, "date", &date);
    int userId = 1;
    JsonGetInt(payload, "user_id", &userId);
    if (extraOut) *extraOut = ",\"tasks\":" + DayLoopTaskListJson(date, userId);
    return "";
  }
  if (op == "day.task_upsert") {
    DayLoopEnsureSchema();
    int userId = 1;
    JsonGetInt(payload, "user_id", &userId);
    std::string out;
    if (!DayLoopTaskUpsert(payload, userId, &out)) return "task_upsert_failed";
    PublishDayLoopMirror(behaviorDir);
    if (extraOut) *extraOut = ",\"task\":" + out;
    return "";
  }
  if (op == "day.task_set_done") {
    int userId = 1;
    long long id = 0;
    bool done = false;
    JsonGetInt(payload, "user_id", &userId);
    int idInt = 0;
    if (JsonGetInt(payload, "id", &idInt)) id = idInt;
    JsonGetBool(payload, "done", &done);
    if (!DayLoopTaskSetDone(id, done, userId)) return "task_set_done_failed";
    PublishDayLoopMirror(behaviorDir);
    return "";
  }
  if (op == "day.task_delete") {
    int userId = 1;
    int idInt = 0;
    JsonGetInt(payload, "user_id", &userId);
    if (!JsonGetInt(payload, "id", &idInt)) return "bad_payload";
    if (!DayLoopTaskDelete(idInt, userId)) return "task_delete_failed";
    PublishDayLoopMirror(behaviorDir);
    return "";
  }
  if (op == "softland.set_bedtime") {
    return DayLoopSetBedtime(behaviorDir, payload);
  }
  if (op == "softland.emergency_winddown") {
    return DayLoopEmergencyWinddown(behaviorDir, payload);
  }
  if (op == "plan.import_from_date") {
    int userId = 1;
    JsonGetInt(payload, "user_id", &userId);
    return DayLoopImportFromDate(payload, userId, extraOut);
  }

  // Device lock (hosts) — independent of SoftLand SoT
  if (op == "device_block.status") {
    auto st = DeviceBlockStatus(behaviorDir);
    if (extraOut) *extraOut = DeviceBlockStatusJsonExtra(st);
    return "";
  }
  if (op == "device_block.save") {
    DeviceBlockSettings patch;
    DeviceBlockLoadSettings(behaviorDir, patch);
    bool hasEnabled = false, hasPorn = false, hasWatch = false, hasSocial = false, hasExtra = false;
    bool b = false;
    if (JsonGetBool(payload, "enabled", &b)) {
      patch.enabled = b;
      hasEnabled = true;
    }
    if (JsonGetBool(payload, "block_porn", &b)) {
      patch.block_porn = b;
      hasPorn = true;
    }
    if (JsonGetBool(payload, "block_watch", &b)) {
      patch.block_watch = b;
      hasWatch = true;
    }
    if (JsonGetBool(payload, "block_social", &b)) {
      patch.block_social = b;
      hasSocial = true;
    }
    std::string confirm;
    JsonGetString(payload, "confirm", &confirm);
    std::string err;
    if (!DeviceBlockPatchSettings(behaviorDir, patch, hasEnabled, hasPorn, hasWatch, hasSocial,
                                  hasExtra, confirm, &err))
      return err.empty() ? "save_failed" : err;
    bool applyNow = false;
    JsonGetBool(payload, "apply_now", &applyNow);
    DeviceBlockApplyResult ar;
    if (applyNow) {
      DeviceBlockSettings s;
      DeviceBlockLoadSettings(behaviorDir, s);
      ar = s.enabled ? DeviceBlockApplyHosts(behaviorDir) : DeviceBlockRemoveHosts(behaviorDir);
    }
    auto st = DeviceBlockStatus(behaviorDir);
    if (extraOut) {
      *extraOut = DeviceBlockStatusJsonExtra(st);
      if (applyNow) *extraOut += DeviceBlockApplyJsonExtra(ar);
    }
    return "";
  }
  if (op == "device_block.apply") {
    std::string confirm;
    JsonGetString(payload, "confirm", &confirm);
    DeviceBlockSettings s;
    DeviceBlockLoadSettings(behaviorDir, s);
    if (!s.enabled) {
      std::string err;
      if (!DeviceBlockTrySetEnabled(behaviorDir, true, confirm, &err))
        return err.empty() ? "confirm_required" : err;
    }
    auto ar = DeviceBlockApplyHosts(behaviorDir);
    auto st = DeviceBlockStatus(behaviorDir);
    if (extraOut) {
      *extraOut = DeviceBlockStatusJsonExtra(st) + DeviceBlockApplyJsonExtra(ar);
    }
    if (!ar.ok) return ar.error.empty() ? "apply_failed" : ar.error;
    return "";
  }
  if (op == "device_block.remove") {
    std::string confirm;
    JsonGetString(payload, "confirm", &confirm);
    std::string err;
    if (!DeviceBlockTrySetEnabled(behaviorDir, false, confirm, &err))
      return err.empty() ? "confirm_required" : err;
    auto ar = DeviceBlockRemoveHosts(behaviorDir);
    auto st = DeviceBlockStatus(behaviorDir);
    if (extraOut) {
      *extraOut = DeviceBlockStatusJsonExtra(st) + DeviceBlockApplyJsonExtra(ar);
    }
    if (!ar.ok) return ar.error.empty() ? "remove_failed" : ar.error;
    return "";
  }
  if (op == "device_block.refresh_list") {
    bool force = false;
    JsonGetBool(payload, "force", &force);
    std::string kick = DeviceBlockRefreshListKick(behaviorDir, force);
    if (extraOut) *extraOut = ",\"refresh\":\"" + kick + "\"";
    return "";
  }

  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return "store_load_failed";

  if (op == "softland.set_enabled") {
    bool en = false;
    if (!JsonGetBool(payload, "enabled", &en)) return "bad_payload";
    // Commitment friction: turning SoftLand *off* requires typed confirm (server-enforced).
    // Matches Focus UI prompt ("UNLOCK"). Pipe clients cannot skip this.
    if (!en && s.softland_enabled) {
      if (!ConfirmMatches(payload, "UNLOCK")) return "confirm_required";
    }
    // Turning SoftLand *on* before Bible + Confirm today is the morning gate itself —
    // allowed. Arm stays separate (only day.confirm_plan / reward / pass couple Arm).
    s.softland_enabled = en;
    s.updated_at = IsoLocalNow();
    ProductivityApplyCacheToDocument(s);
    if (!ProductivitySaveSoftland(s)) return "store_save_failed";
    ProductivityBumpSeq();
    PublishSoftlandMirror(behaviorDir, s);
    return "";
  }
  if (op == "softland.set_incubation") {
    // max_incubations_per_hour = 1 (break_reward.py DEFAULT_CONFIG)
    if (ProductivityIncubationStartsSinceIso(AddMinutesLocalIso(-60)) >= 1)
      return "incubation_rate_limited";
    std::string until;
    int minutes = 0;
    if (JsonGetString(payload, "until_iso", &until) && !until.empty()) {
      s.incubation_until = until;
    } else if (JsonGetInt(payload, "minutes", &minutes) && minutes > 0) {
      s.incubation_until = AddMinutesLocalIso(minutes);
    } else {
      s.incubation_until = AddMinutesLocalIso(8);  // break_minutes default
    }
    std::string err = SaveAndPublish(s, behaviorDir);
    if (!err.empty()) return err;
    ProductivityLedgerAdd("incubation", 0, "incubation start", "gateway");
    return "";
  }
  if (op == "softland.clear_incubation") {
    s.incubation_until.clear();
    s.updated_at = IsoLocalNow();
    ProductivityApplyCacheToDocument(s);
    if (!ProductivitySaveSoftland(s)) return "store_save_failed";
    ProductivityBumpSeq();
    PublishSoftlandMirror(behaviorDir, s);
    return "";
  }
  if (op == "softland.spend_free") {
    int minutes = 0;
    if (!JsonGetInt(payload, "minutes", &minutes) || minutes <= 0) return "bad_payload";
    int need = minutes * 60;
    if (s.earned_ledger_seconds < need) return "insufficient_ledger";
    s.earned_ledger_seconds -= need;
    s.free_until = ExtendFreeWindowIso(s.free_until, minutes);
    std::string err = SaveAndPublish(s, behaviorDir);
    if (!err.empty()) return err;
    ProductivityLedgerAdd("spend", need, "free window " + std::to_string(minutes) + "m", "gateway");
    return "";
  }
  if (op == "softland.patch_site_rules") {
    std::string section;
    if (!JsonGetRaw(s.document_json, "site_rules", &section)) return "policy_key_missing";
    bool any = false;
    for (const char* key : {"allow_extra", "watch_extra", "block_extra"}) {
      std::string raw;
      if (!JsonGetRaw(payload, key, &raw)) continue;
      std::string clean;
      if (!SanitizedHostArray(raw, &clean)) return "bad_payload";
      if (!ProductivityReplaceJsonValue(section, key, clean)) return "policy_key_missing";
      any = true;
    }
    if (!any) return "bad_payload";
    if (!ProductivityReplaceJsonValue(s.document_json, "site_rules", section))
      return "policy_key_missing";
    return SaveAndPublish(s, behaviorDir);
  }
  if (op == "softland.patch_schedules") {
    // Patch inside the schedules object so generic keys ("enabled") can never
    // hit a same-named key elsewhere in the document.
    std::string section;
    if (!JsonGetRaw(s.document_json, "schedules", &section)) return "policy_key_missing";
    bool any = false;
    bool enabled = false;
    if (JsonGetBool(payload, "enabled", &enabled)) {
      if (!ProductivityReplaceJsonValue(section, "enabled", enabled ? "true" : "false"))
        return "policy_key_missing";
      any = true;
    }
    std::string windows;
    if (JsonGetRaw(payload, "windows", &windows)) {
      if (!RawJsonAcceptable(windows, '[', 64 * 1024)) return "bad_payload";
      if (!ProductivityReplaceJsonValue(section, "windows", windows)) return "policy_key_missing";
      any = true;
    }
    if (!any) return "bad_payload";
    if (!ProductivityReplaceJsonValue(s.document_json, "schedules", section))
      return "policy_key_missing";
    return SaveAndPublish(s, behaviorDir);
  }
  if (op == "softland.patch_mode_flags" || op == "softland.patch_goals") {
    const char* key = op == "softland.patch_mode_flags" ? "mode_flags" : "goals";
    std::string raw;
    if (!JsonGetRaw(payload, key, &raw)) return "bad_payload";
    if (!RawJsonAcceptable(raw, '{', 16 * 1024)) return "bad_payload";
    if (!ProductivityReplaceJsonValue(s.document_json, key, raw)) return "policy_key_missing";
    return SaveAndPublish(s, behaviorDir);
  }
  if (op == "softland.set_reward_day") {
    bool active = false;
    if (!JsonGetBool(payload, "active", &active)) return "bad_payload";
    s.reward_day_active = active;
    std::string until;
    if (active) {
      if (JsonGetString(payload, "free_until", &until) && !until.empty())
        s.free_until = until;
      else
        s.free_until = EndOfLocalDayIso();
    } else {
      s.free_until.clear();
    }
    std::string err = SaveAndPublish(s, behaviorDir);
    if (!err.empty()) return err;
    ProductivityLedgerAdd("reward_day", 0, active ? "reward day on" : "reward day off", "gateway");
    return "";
  }
  if (op == "softland.set_day_pass") {
    std::string date;
    bool spent = false;
    int remaining = 0;
    JsonGetString(payload, "date", &date);
    JsonGetBool(payload, "spent", &spent);
    JsonGetInt(payload, "remaining_seconds", &remaining);
    if (remaining < 0) remaining = 0;
    std::string obj = std::string("{\"date\": ") + (date.empty() ? "null" : "\"" + date + "\"") +
                      ", \"spent\": " + (spent ? "true" : "false") +
                      ", \"remaining_seconds\": " + std::to_string(remaining) + "}";
    if (!ProductivityReplaceJsonValue(s.document_json, "day_pass", obj)) return "policy_key_missing";
    // Legacy callers: spent pass must open SoftLand via free_until (decide ignores day_pass).
    if (spent) {
      if (remaining > 0) {
        int minutes = (remaining + 59) / 60;
        if (minutes < 1) minutes = 1;
        s.free_until = ExtendFreeWindowIso(s.free_until, minutes);
      } else {
        s.free_until = EndOfLocalDayIso();
      }
    }
    return SaveAndPublish(s, behaviorDir);
  }
  if (op == "day.grant_pass") {
    if (!ConfirmMatches(payload, "PASS")) return "confirm_required";
    const std::string today = ProductivityLocalDate();
    if (!DayLoopBibleDoneToday(s.document_json)) return "bible_required";
    const bool already = ProductivityPassGrantedToday();
    if (!already && ProductivityPassesUsedThisWeek() >= 2) return "pass_quota_exhausted";
    if (!already && !ProductivityInsertPass(today, ProductivityWeekStart()))
      return "store_save_failed";

    // One free-window mechanism: SoftLand decide already honours free_until.
    s.softland_enabled = true;
    s.free_until = EndOfLocalDayIso();
    std::string obj = "{\"date\": \"" + today + "\", \"spent\": true, \"remaining_seconds\": 0}";
    if (!ProductivityReplaceJsonValue(s.document_json, "day_pass", obj)) return "policy_key_missing";
    ProductivityReplaceJsonValue(s.document_json, "plan_confirmed", "true");
    ProductivityReplaceJsonValue(s.document_json, "plan_confirmed_for_date",
                                 std::string("\"") + today + "\"");
    std::string err = SaveAndPublish(s, behaviorDir);
    if (!err.empty()) return err;
    DayLoopClearStudyTempKills(behaviorDir);
    if (!already) ProductivityLedgerAdd("day_pass", 0, "day pass granted " + today, "gateway");
    return "";
  }
  if (op == "day.mark_event") {
    std::string event;
    if (!JsonGetString(payload, "event", &event) || event.empty()) return "bad_payload";
    // Rates copied from backend/behavior/break_reward.py DEFAULT_CONFIG.
    int minutes = 0;
    if (event == "chapter_done")
      minutes = 15;
    else if (event == "plan_confirmed")
      minutes = 10;
    else if (event == "daily_goal")
      minutes = 30;
    else if (event == "bite_done")
      minutes = 0;
    else
      return "bad_payload";

    const std::string today = ProductivityLocalDate();
    if (ProductivityDayEventSeconds(today, event, nullptr)) return "event_already_recorded";

    // Daily cap 60 min: credit only the remainder, never negative.
    const int capSeconds = 60 * 60;
    int used = ProductivityDayEarnedSecondsToday();
    int want = minutes * 60;
    int credit = want;
    if (used + credit > capSeconds) credit = capSeconds - used;
    if (credit < 0) credit = 0;

    if (!ProductivityInsertDayEvent(today, event, credit)) return "store_save_failed";
    if (credit > 0) {
      s.earned_ledger_seconds += credit;
      std::string err = SaveAndPublish(s, behaviorDir);
      if (!err.empty()) return err;
      ProductivityLedgerAdd("earn", credit, event, "gateway");
    }
    if (extraOut)
      *extraOut = ",\"credited_seconds\":" + std::to_string(credit) +
                  ",\"balance_seconds\":" + std::to_string(s.earned_ledger_seconds);
    return "";
  }
  if (op == "reward.status" || op == "day.status") {
    const int qualified = ProductivityRewardCount("qualified");
    const int used = ProductivityRewardCount("used");
    const int granted = ProductivityRewardGranted();
    const int earned = qualified / 4;  // QUALIFYING_DAYS_PER_REWARD
    int available = earned + granted - used;
    if (available < 0) available = 0;
    const int toNext = 4 - (qualified % 4);
    std::string extra = ",\"qualified_days\":" + std::to_string(qualified) +
                        ",\"reward_earned\":" + std::to_string(earned) +
                        ",\"reward_granted\":" + std::to_string(granted) +
                        ",\"reward_spent\":" + std::to_string(used) +
                        ",\"reward_available\":" + std::to_string(available) +
                        ",\"days_to_next_reward\":" + std::to_string(toNext);
    if (op == "day.status") {
      extra += ",\"passes_limit\":2,\"passes_used\":" +
               std::to_string(ProductivityPassesUsedThisWeek()) +
               ",\"pass_today\":" + (ProductivityPassGrantedToday() ? "true" : "false") +
               ",\"earned_today_seconds\":" + std::to_string(ProductivityDayEarnedSecondsToday()) +
               ",\"balance_seconds\":" + std::to_string(s.earned_ledger_seconds) +
               ",\"free_until\":" + (s.free_until.empty() ? "null" : "\"" + s.free_until + "\"") +
               ",\"reward_day_active\":" + (s.reward_day_active ? "true" : "false");
      PublishDayStatusMirror(behaviorDir);
    }
    if (extraOut) *extraOut = extra;
    return "";
  }
  if (op == "reward.claim") {
    if (!ConfirmMatches(payload, "REWARD")) return "confirm_required";
    const std::string today = ProductivityLocalDate();
    if (!DayLoopBibleDoneToday(s.document_json)) return "bible_required";
    const int qualified = ProductivityRewardCount("qualified");
    const int used = ProductivityRewardCount("used");
    const int available = qualified / 4 + ProductivityRewardGranted() - used;
    if (available <= 0) return "no_reward_available";
    if (s.reward_day_active) return "already_unlocked";
    if (!ProductivityRewardMark("used", today)) return "store_save_failed";
    s.reward_day_active = true;
    s.softland_enabled = true;
    s.free_until = EndOfLocalDayIso();
    ProductivityReplaceJsonValue(s.document_json, "plan_confirmed", "true");
    ProductivityReplaceJsonValue(s.document_json, "plan_confirmed_for_date",
                                 std::string("\"") + today + "\"");
    std::string err = SaveAndPublish(s, behaviorDir);
    if (!err.empty()) return err;
    DayLoopClearStudyTempKills(behaviorDir);
    ProductivityLedgerAdd("reward_day", 0, "reward day claimed " + today, "gateway");
    return "";
  }
  if (op == "reward.mark_qualified") {
    // Bridge until P5c: Python still decides qualification; enforcer owns the ledger.
    const std::string today = ProductivityLocalDate();
    if (!ProductivityRewardMark("qualified", today)) return "store_save_failed";
    if (extraOut) {
      const int qualified = ProductivityRewardCount("qualified");
      const int usedR = ProductivityRewardCount("used");
      const int granted = ProductivityRewardGranted();
      int available = qualified / 4 + granted - usedR;
      if (available < 0) available = 0;
      *extraOut = ",\"qualified_days\":" + std::to_string(qualified) +
                  ",\"reward_available\":" + std::to_string(available);
    }
    return "";
  }
  if (op == "reward.grant_credits") {
    int count = 0;
    if (!JsonGetInt(payload, "count", &count) || count <= 0) return "bad_payload";
    if (!ProductivityRewardSetGranted(ProductivityRewardGranted() + count))
      return "store_save_failed";
    if (extraOut) {
      const int granted = ProductivityRewardGranted();
      *extraOut = ",\"reward_granted\":" + std::to_string(granted);
    }
    return "";
  }
  if (op == "ledger.add") {
    int seconds = 0;
    std::string kind, note;
    if (!JsonGetInt(payload, "seconds", &seconds) || seconds == 0) return "bad_payload";
    if (!JsonGetString(payload, "kind", &kind) || kind.empty()) kind = "adjust";
    if (kind != "earn" && kind != "spend" && kind != "adjust") return "bad_payload";
    JsonGetString(payload, "note", &note);
    long long balance = (long long)s.earned_ledger_seconds + (kind == "spend" ? -seconds : seconds);
    if (balance < 0) balance = 0;
    s.earned_ledger_seconds = (int)balance;
    std::string err = SaveAndPublish(s, behaviorDir);
    if (!err.empty()) return err;
    ProductivityLedgerAdd(kind.c_str(), seconds, note, "gateway");
    return "";
  }
  if (op == "ledger.snapshot") {
    int limit = 0;
    if (!JsonGetInt(payload, "limit", &limit) || limit <= 0) limit = 50;
    if (extraOut)
      *extraOut = ",\"ledger\":" + ProductivityLedgerRecentJson(limit) +
                  ",\"balance_seconds\":" + std::to_string(s.earned_ledger_seconds);
    return "";
  }
  if (op == "pending.add") {
    std::string innerOp, innerPayload, applyAfter;
    int delay = 0;
    if (!JsonGetString(payload, "op", &innerOp) || innerOp.empty()) return "bad_payload";
    if (!JsonGetRaw(payload, "payload", &innerPayload)) innerPayload = "{}";
    if (!RawJsonAcceptable(innerPayload, '{', 64 * 1024)) return "bad_payload";
    if (JsonGetString(payload, "apply_after_iso", &applyAfter) && !applyAfter.empty()) {
      // caller-supplied stamp
    } else if (JsonGetInt(payload, "delay_minutes", &delay) && delay > 0) {
      applyAfter = AddMinutesLocalIso(delay);
    } else {
      return "bad_payload";
    }
    if (!ProductivityInsertPending(applyAfter, innerOp, innerPayload)) return "pending_insert_failed";
    if (extraOut) *extraOut = ",\"apply_after\":\"" + applyAfter + "\"";
    return "";
  }
  if (op == "pending.list") {
    if (extraOut) *extraOut = ",\"pending\":" + ProductivityPendingListJson(20);
    return "";
  }
  if (op == "pending.cancel") {
    int id = 0;
    if (!JsonGetInt(payload, "id", &id) || id <= 0) return "bad_payload";
    if (!ProductivityMarkPending(id, "cancelled")) return "pending_update_failed";
    if (extraOut) *extraOut = ",\"pending\":" + ProductivityPendingListJson(20);
    return "";
  }
  // Phase 6a — planner CRUD (SQLite planner_* via gateway; no SoftLand mutate).
  if (op == "plan.list") {
    std::string from, to;
    int userId = 1;
    if (!JsonGetString(payload, "from", &from) || from.empty()) return "bad_payload";
    if (!JsonGetString(payload, "to", &to) || to.empty()) return "bad_payload";
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    if (extraOut) *extraOut = ",\"blocks\":" + ProductivityPlanListJson(from, to, userId);
    return "";
  }
  if (op == "plan.get") {
    int id = 0;
    int userId = 1;
    if (!JsonGetInt(payload, "id", &id) || id <= 0) return "bad_payload";
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    std::string block = ProductivityPlanGetJson(id, userId);
    if (block.empty()) return "not_found";
    if (extraOut) *extraOut = ",\"block\":" + block;
    return "";
  }
  if (op == "plan.upsert") {
    int userId = 1;
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    std::string blockJson;
    if (!ProductivityPlanUpsert(payload, userId, &blockJson) || blockJson.empty())
      return "store_save_failed";
    PublishPlanBlocksMirror(behaviorDir);
    if (extraOut) *extraOut = ",\"block\":" + blockJson;
    return "";
  }
  if (op == "plan.delete") {
    int id = 0;
    int userId = 1;
    if (!JsonGetInt(payload, "id", &id) || id <= 0) return "bad_payload";
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    if (!ProductivityPlanDelete(id, userId)) return "not_found";
    PublishPlanBlocksMirror(behaviorDir);
    if (extraOut) *extraOut = ",\"deleted\":true,\"block_id\":" + std::to_string(id);
    return "";
  }
  if (op == "plan.clear_day") {
    int userId = 1;
    std::string date;
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    JsonGetString(payload, "date", &date);
    int n = ProductivityPlanClearDay(userId, date);
    if (n < 0) return "store_save_failed";
    PublishPlanBlocksMirror(behaviorDir);
    if (extraOut) *extraOut = ",\"deleted\":" + std::to_string(n);
    return "";
  }
  if (op == "routine.list") {
    int userId = 1;
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    if (extraOut) *extraOut = ",\"routines\":" + ProductivityRoutineListJson(userId);
    return "";
  }
  if (op == "routine.upsert") {
    int userId = 1;
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    std::string routineJson;
    if (!ProductivityRoutineUpsert(payload, userId, &routineJson) || routineJson.empty())
      return "store_save_failed";
    if (extraOut) *extraOut = ",\"routine\":" + routineJson;
    return "";
  }
  if (op == "routine.delete") {
    int id = 0;
    int userId = 1;
    if (!JsonGetInt(payload, "id", &id) || id <= 0) return "bad_payload";
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    if (!ProductivityRoutineDelete(id, userId)) return "not_found";
    if (extraOut) *extraOut = ",\"deleted\":true,\"routine_id\":" + std::to_string(id);
    return "";
  }
  // Phase 6b — force-sync active plan block → SoftLand
  if (op == "plan.apply_gate") {
    int userId = 1;
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    const bool applied = ApplyActivePlanToSoftland(behaviorDir, userId);
    if (extraOut)
      *extraOut = std::string(",\"applied\":") + (applied ? "true" : "false");
    return "";
  }
  if (op == "plan.start") {
    int id = 0;
    int userId = 1;
    if (!JsonGetInt(payload, "id", &id) || id <= 0) return "bad_payload";
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    std::string blockJson;
    if (!ProductivityPlanStart(id, userId, &blockJson) || blockJson.empty()) return "not_found";
    if (extraOut) *extraOut = ",\"block\":" + blockJson;
    return "";
  }
  if (op == "plan.complete") {
    int id = 0;
    int userId = 1;
    int minutes = -1;
    if (!JsonGetInt(payload, "id", &id) || id <= 0) return "bad_payload";
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    JsonGetInt(payload, "minutes_spent", &minutes);
    std::string blockJson;
    if (!ProductivityPlanComplete(id, userId, minutes, &blockJson) || blockJson.empty())
      return "not_found";
    if (extraOut) *extraOut = ",\"block\":" + blockJson;
    return "";
  }
  if (op == "plan.roll_forward") {
    // Product rule: unfinished plans do not carry to tomorrow/today.
    return "carry_disabled";
  }
  if (op == "routine.apply") {
    int userId = 1;
    std::string date;
    bool skip = true;
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    JsonGetString(payload, "date", &date);
    JsonGetBool(payload, "skip_overlaps", &skip);
    int n = ProductivityRoutineApply(userId, date, skip);
    if (n < 0) return "store_save_failed";
    PublishPlanBlocksMirror(behaviorDir);
    if (extraOut) *extraOut = ",\"created\":" + std::to_string(n);
    return "";
  }
  if (op == "plan.overlay") {
    std::string from, to;
    int userId = 1;
    if (!JsonGetString(payload, "from", &from) || from.empty()) return "bad_payload";
    if (!JsonGetString(payload, "to", &to) || to.empty()) return "bad_payload";
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    if (extraOut) *extraOut = ",\"overlay\":" + ProductivityPlanOverlayJson(from, to, userId);
    return "";
  }
  if (op == "plan.adherence") {
    std::string day;
    int userId = 1;
    JsonGetString(payload, "day", &day);
    JsonGetInt(payload, "user_id", &userId);
    if (userId <= 0) userId = 1;
    if (extraOut)
      *extraOut = ",\"adherence\":" + ProductivityPlanAdherenceJson(day, userId);
    return "";
  }

  // Focus life content — Journal + Bible (no Study :8000).
  {
    std::wstring dataDir = behaviorDir;
    size_t slash = dataDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
    std::wstring bibleDir = dataDir + L"\\bible";

    if (op == "journal.summary") {
      std::string day;
      int userId = 1;
      JsonGetString(payload, "day", &day);
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      LifeEnsureTables();
      if (extraOut) *extraOut = ",\"summary\":" + LifeJournalSummaryJson(day, userId);
      return "";
    }
    if (op == "journal.log") {
      int limit = 30;
      int userId = 1;
      JsonGetInt(payload, "limit", &limit);
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      if (extraOut) *extraOut = ",\"entries\":" + LifeJournalLogJson(limit, userId);
      return "";
    }
    if (op == "journal.upsert") {
      int userId = 1;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      std::string entry;
      std::string contentCheck;
      if (!JsonGetString(payload, "content", &contentCheck) || contentCheck.empty())
        return "bad_payload";
      if (!LifeJournalUpsert(payload, userId, &entry) || entry.empty()) return "store_save_failed";
      if (extraOut) *extraOut = ",\"entry\":" + entry;
      return "";
    }
    if (op == "bible.today" || op == "bible.state") {
      int userId = 1;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      if (extraOut) *extraOut = ",\"state\":" + LifeBibleTodayJson(userId, bibleDir);
      return "";
    }
    if (op == "bible.tick") {
      int userId = 1;
      std::string book;
      int chapter = 1;
      bool done = true;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      JsonGetString(payload, "book", &book);
      JsonGetInt(payload, "chapter", &chapter);
      JsonGetBool(payload, "done", &done);
      std::string state;
      if (!LifeBibleTick(userId, book, chapter, done, bibleDir, behaviorDir, &state))
        return "tick_failed";
      if (extraOut) *extraOut = ",\"state\":" + state;
      return "";
    }
    if (op == "bible.heartbeat") {
      int userId = 1;
      std::string book;
      int chapter = 1;
      bool focused = true;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      JsonGetString(payload, "book", &book);
      JsonGetInt(payload, "chapter", &chapter);
      JsonGetBool(payload, "focused", &focused);
      std::string state;
      if (!LifeBibleHeartbeat(userId, book, chapter, focused, bibleDir, &state))
        return "heartbeat_failed";
      if (extraOut) *extraOut = ",\"state\":" + state;
      return "";
    }
    if (op == "bible.devotion.today") {
      int userId = 1;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      PublishBibleDevotionMirror(behaviorDir, bibleDir, userId);
      if (extraOut) *extraOut = ",\"devotion\":" + LifeBibleDevotionTodayJson(userId, bibleDir);
      return "";
    }
    if (op == "bible.devotion.done") {
      int userId = 1;
      std::string slot;
      bool done = true;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      JsonGetString(payload, "slot", &slot);
      JsonGetBool(payload, "done", &done);
      std::string out;
      if (!LifeBibleDevotionDone(userId, slot, done, bibleDir, behaviorDir, &out))
        return "devotion_failed";
      if (extraOut) *extraOut = ",\"devotion\":" + out;
      return "";
    }
    if (op == "bible.devotion.notes") {
      int userId = 1;
      std::string slot, notes;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      JsonGetString(payload, "slot", &slot);
      JsonGetString(payload, "notes", &notes);
      std::string out;
      if (!LifeBibleDevotionNotes(userId, slot, notes, bibleDir, &out)) return "devotion_failed";
      if (extraOut) *extraOut = ",\"devotion\":" + out;
      return "";
    }
    if (op == "bible.devotion.assign") {
      int userId = 1;
      std::string slot, book;
      int chapter = 1;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      JsonGetString(payload, "slot", &slot);
      JsonGetString(payload, "book", &book);
      JsonGetInt(payload, "chapter", &chapter);
      std::string out;
      if (!LifeBibleDevotionAssign(userId, slot, book, chapter, bibleDir, &out))
        return "devotion_failed";
      if (extraOut) *extraOut = ",\"devotion\":" + out;
      return "";
    }
    if (op == "bible.devotion.step") {
      int userId = 1;
      std::string slot;
      int delta = 1;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      JsonGetString(payload, "slot", &slot);
      JsonGetInt(payload, "delta", &delta);
      std::string out;
      if (!LifeBibleDevotionStep(userId, slot, delta, bibleDir, &out)) return "devotion_failed";
      if (extraOut) *extraOut = ",\"devotion\":" + out;
      return "";
    }
    if (op == "bible.devotion.prayer") {
      int userId = 1;
      std::string slot, text;
      bool reset = false;
      JsonGetInt(payload, "user_id", &userId);
      if (userId <= 0) userId = 1;
      JsonGetString(payload, "slot", &slot);
      JsonGetString(payload, "text", &text);
      JsonGetBool(payload, "reset", &reset);
      std::string out;
      if (!LifeBibleDevotionPrayer(userId, slot, text, reset, bibleDir, &out))
        return "devotion_failed";
      if (extraOut) *extraOut = ",\"devotion\":" + out;
      return "";
    }
  }

  if (op == "arm.set") {
    EnforcerSnapshot cur;
    std::string loadErr;
    std::wstring polPath = behaviorDir + L"\\enforcer_policy.json";
    // Load existing via LoadEnforcerSnapshot needs dbPath — synthesize from behaviorDir parent
    std::wstring dataDir = behaviorDir;
    size_t slash = dataDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dataDir = dataDir.substr(0, slash);
    std::wstring dbPath = dataDir + L"\\productivity.db";
    if (GetFileAttributesW(dbPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
      dbPath = dataDir + L"\\vocab_app.db";  // legacy
    }
    LoadEnforcerSnapshot(dbPath, cur, loadErr);

    // Snapshot *persisted* commitment state before any payload mutation.
    const EnforcerSnapshot prev = cur;
    const bool wasArmedOrLocked = prev.armed || prev.locked;

    bool armed = cur.armed;
    bool locked = cur.locked;
    bool incubation = cur.incubation;
    bool anti = cur.anti_tamper;
    bool protect = false;
    {
      bool hb = false;
      if (JsonGetBool(payload, "hard_block_armed", &hb))
        armed = hb;
      else
        JsonGetBool(payload, "armed", &armed);
    }
    if (!JsonGetBool(payload, "gate_locked", &locked)) locked = armed;
    JsonGetBool(payload, "incubation_active", &incubation);
    JsonGetBool(payload, "anti_tamper", &anti);
    JsonGetBool(payload, "protect_uninstall", &protect);

    std::string mode, pwd, phrase, provided;
    const bool hasMode = JsonGetString(payload, "lock_mode", &mode) && !mode.empty();
    JsonGetString(payload, "unlock_password", &pwd);
    JsonGetString(payload, "unlock_phrase", &phrase);
    JsonGetString(payload, "provided_unlock", &provided);
    int lockUntil = 0;
    if (JsonGetInt(payload, "lock_until_unix", &lockUntil)) cur.lock_until_unix = lockUntil;

    const bool disarming = !armed && wasArmedOrLocked;
    const bool weakeningLock =
        hasMode && LockModeIsStrong(prev.lock_mode) && !LockModeIsStrong(mode);
    // Gate unlock against *previous* lock_mode/secret — never against payload lock_mode.
    // Prevents: {"armed":false,"lock_mode":"none"} clearing password then skipping the check.
    if ((disarming || weakeningLock) && LockModeIsStrong(prev.lock_mode)) {
      if (!RequirePersistedUnlock(prev, provided)) return "unlock_failed";
    }

    if (hasMode)
      cur.lock_mode = mode;
    else if (cur.lock_mode.empty())
      cur.lock_mode = "none";

    // exes array
    size_t exesKey = payload.find("\"exes\"");
    if (exesKey != std::string::npos) {
      size_t lb = payload.find('[', exesKey);
      size_t rb = payload.find(']', lb == std::string::npos ? 0 : lb);
      if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
        cur.exes.clear();
        std::string arr = payload.substr(lb, rb - lb + 1);
        size_t i = 0;
        while (i < arr.size()) {
          if (arr[i] == '"') {
            size_t j = i + 1;
            std::string tok;
            while (j < arr.size() && arr[j] != '"') {
              if (arr[j] == '\\') {
                if (!DecodeJsonEscape(arr, j, &tok)) break;
                continue;
              }
              tok.push_back(arr[j++]);
            }
            if (!tok.empty()) {
              for (auto& c : tok) c = (char)tolower((unsigned char)c);
              std::wstring w(tok.begin(), tok.end());
              cur.exes.push_back(w);
            }
            i = (j < arr.size()) ? j + 1 : j;
            continue;
          }
          ++i;
        }
      }
    }

    // Changing stored secrets while strong-locked also requires unlock.
    if (LockModeIsStrong(prev.lock_mode) && wasArmedOrLocked) {
      bool secretChange = false;
      if (prev.lock_mode == "password" && !pwd.empty() && pwd != prev.unlock_password)
        secretChange = true;
      if (prev.lock_mode == "phrase" && !phrase.empty() && phrase != prev.unlock_phrase)
        secretChange = true;
      if (secretChange && !RequirePersistedUnlock(prev, provided)) return "unlock_failed";
    }

    if (!pwd.empty()) cur.unlock_password = pwd;
    if (!phrase.empty()) cur.unlock_phrase = phrase;
    cur.armed = armed;
    cur.locked = locked;
    cur.incubation = incubation;
    cur.anti_tamper = anti;
    if (cur.lock_mode.empty()) cur.lock_mode = "none";

    // Disarmed policy must not keep a strong lock_mode. ApplyStickyLock re-arms on
    // the next tick if password/phrase mode remains while hard_block_armed is false
    // (provided_unlock is not persisted in the JSON mirror). Comment in policy_db:
    // "API clears mode on success." Keep unlock_password/phrase so the next Arm can
    // re-enable the same secret without retyping.
    if (!cur.armed) {
      cur.lock_mode = "none";
      cur.locked = false;
    }

    if (!WriteEnforcerPolicyJson(polPath, cur, anti, protect)) return "arm_write_failed";
    // Keep SQLite enforcer_runtime aligned with JSON (JSON remains authoritative).
    SyncEnforcerRuntimeSqlite(dbPath, cur);
    ProductivityBumpSeq();
    return "";
  }
  return "unknown_op";
}

void EnsurePipe() {
  if (gPipe != INVALID_HANDLE_VALUE) return;
  // LocalSystem service default DACL denies interactive user clients (Focus /
  // msg-host / scripts) — Access denied (5). Allow SYSTEM + Admins + Interactive
  // + Authenticated Users to connect on this machine-local control pipe.
  PSECURITY_DESCRIPTOR sd = nullptr;
  SECURITY_ATTRIBUTES* psa = nullptr;
  SECURITY_ATTRIBUTES sa{};
  if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)(A;;GA;;;AU)", SDDL_REVISION_1, &sd, nullptr) &&
      sd) {
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    psa = &sa;
  }
  // Two instances: one live listener + one spare while recycling (no listen gap).
  // PIPE_NOWAIT: never block the kill/track loop waiting for a client.
  gPipe = CreateNamedPipeW(kPipeName, PIPE_ACCESS_DUPLEX,
                           PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT, 2, 64 * 1024,
                           64 * 1024, 0, psa);
  if (sd) LocalFree(sd);
}

/**
 * Recycle the listening instance. Create the next listener BEFORE closing the
 * old handle so clients never see a gap (that gap was a common Focus timeout).
 */
void ResetPipe() {
  HANDLE old = gPipe;
  gPipe = INVALID_HANDLE_VALUE;
  EnsurePipe();
  if (gPipe != INVALID_HANDLE_VALUE) {
    ConnectNamedPipe(gPipe, nullptr);  // arm listen (may return ERROR_PIPE_CONNECTED)
  }
  if (old != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(old);
    CloseHandle(old);
  }
}

}  // namespace

std::string GatewayApplyOp(const std::string& op, const std::string& payloadJson,
                           const std::wstring& behaviorDir) {
  return HandleOp(op, payloadJson, behaviorDir, nullptr);
}

void CmdGatewayInit() {
  EnsurePipe();
  if (gPipe != INVALID_HANDLE_VALUE) ConnectNamedPipe(gPipe, nullptr);
}

void CmdGatewayShutdown() {
  if (gPipe != INVALID_HANDLE_VALUE) {
    CloseHandle(gPipe);
    gPipe = INVALID_HANDLE_VALUE;
  }
}

void CmdGatewayPoll(const std::wstring& behaviorDir) {
  // Serve a burst of waiting clients, but stop as soon as no one is connected
  // so we never open extra idle instances.
  for (int n = 0; n < 4; ++n) {
    EnsurePipe();
    if (gPipe == INVALID_HANDLE_VALUE) return;

    BOOL connected =
        ConnectNamedPipe(gPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!connected) {
      return;  // still listening / no client
    }

    char buf[64 * 1024];
    DWORD read = 0;
    if (!ReadFile(gPipe, buf, sizeof(buf) - 1, &read, nullptr) || read == 0) {
      if (GetLastError() != ERROR_NO_DATA) ResetPipe();
      return;
    }
    buf[read] = 0;
    std::string req(buf, read);
    std::string op, id;
    JsonGetString(req, "op", &op);
    JsonGetString(req, "id", &id);
    std::string payload = ExtractPayload(req);
    std::string extra;
    std::string err = HandleOp(op, payload, behaviorDir, &extra);

    ProductivitySoftland s;
    ProductivityLoadSoftland(s);
    ProductivityGatewayMeta meta;
    ProductivityLoadMeta(meta);

    std::string freeJs = s.free_until.empty() ? "null" : ("\"" + s.free_until + "\"");
    std::string incJs = s.incubation_until.empty() ? "null" : ("\"" + s.incubation_until + "\"");
    std::string out;
    if (err.empty()) {
      out = std::string("{\"ok\":true,\"id\":\"") + id + "\",\"gateway_seq\":" +
            std::to_string(meta.gateway_seq) + ",\"as_of\":\"" + IsoLocalNow() +
            "\",\"softland_enabled\":" + (s.softland_enabled ? "true" : "false") +
            ",\"free_until\":" + freeJs + ",\"incubation_until\":" + incJs +
            ",\"reward_day_active\":" + (s.reward_day_active ? "true" : "false") +
            ",\"earned_ledger_seconds\":" + std::to_string(s.earned_ledger_seconds) + extra + "}";
    } else {
      out = std::string("{\"ok\":false,\"id\":\"") + id + "\",\"error\":\"" + err + "\"}";
    }

    DWORD written = 0;
    WriteFile(gPipe, out.data(), (DWORD)out.size(), &written, nullptr);
    FlushFileBuffers(gPipe);
    ResetPipe();
  }
}
