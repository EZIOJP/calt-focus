#include "policy_db.h"
#include "sqlite3.h"

#include <windows.h>

#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
  return w;
}

std::wstring ToLower(std::wstring s) {
  for (auto& c : s) c = (wchar_t)towlower(c);
  return s;
}

void ParseExeArray(const std::string& json, std::vector<std::wstring>& out) {
  out.clear();
  size_t i = 0;
  while (i < json.size()) {
    if (json[i] == '"') {
      size_t j = i + 1;
      std::string tok;
      while (j < json.size() && json[j] != '"') {
        if (json[j] == '\\' && j + 1 < json.size()) {
          tok.push_back(json[j + 1]);
          j += 2;
          continue;
        }
        tok.push_back(json[j++]);
      }
      if (!tok.empty()) out.push_back(ToLower(Widen(tok)));
      i = (j < json.size()) ? j + 1 : j;
      continue;
    }
    ++i;
  }
}

bool JsonBoolNear(const std::string& body, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) ++i;
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

bool JsonStringNear(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t q1 = body.find('"', colon + 1);
  if (q1 == std::string::npos) return false;
  size_t q2 = q1 + 1;
  std::string val;
  while (q2 < body.size() && body[q2] != '"') {
    if (body[q2] == '\\' && q2 + 1 < body.size()) {
      val.push_back(body[q2 + 1]);
      q2 += 2;
      continue;
    }
    val.push_back(body[q2++]);
  }
  *out = val;
  return true;
}

bool JsonIntNear(const std::string& body, const char* key, std::int64_t* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) ++i;
  if (i >= body.size()) return false;
  bool neg = false;
  if (body[i] == '-') {
    neg = true;
    ++i;
  }
  if (i >= body.size() || body[i] < '0' || body[i] > '9') return false;
  std::int64_t v = 0;
  while (i < body.size() && body[i] >= '0' && body[i] <= '9') {
    v = v * 10 + (body[i] - '0');
    ++i;
  }
  *out = neg ? -v : v;
  return true;
}

void ApplyStickyLock(EnforcerSnapshot& out) {
  // If JSON tries to disarm while strong lock is unmet, force armed+locked.
  std::string mode = out.lock_mode;
  for (auto& c : mode) c = (char)tolower((unsigned char)c);
  if (mode.empty() || mode == "none") return;

  const std::time_t now = std::time(nullptr);
  bool block_disarm = false;

  if (mode == "timer") {
    if (out.lock_until_unix > 0 && (std::int64_t)now < out.lock_until_unix) {
      block_disarm = true;
    }
  } else if (mode == "password") {
    if (!out.unlock_password.empty() && out.provided_unlock != out.unlock_password) {
      // Disarm only honored when provided_unlock matches (API clears mode on success).
      if (!out.armed || !out.locked) block_disarm = true;
    }
  } else if (mode == "phrase") {
    if (!out.unlock_phrase.empty() && out.provided_unlock != out.unlock_phrase) {
      if (!out.armed || !out.locked) block_disarm = true;
    }
  }

  if (block_disarm) {
    out.armed = true;
    out.locked = true;
  }
}

bool LoadFromSqlite(const std::wstring& dbPath, EnforcerSnapshot& out, std::string& err) {
  sqlite3* db = nullptr;
  std::string path = Narrow(dbPath);
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    err = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
    if (db) sqlite3_close(db);
    return false;
  }
  sqlite3_busy_timeout(db, 2000);

  const char* sql =
      "SELECT hard_block_armed, gate_locked, incubation_active, exes_json "
      "FROM enforcer_runtime ORDER BY id DESC LIMIT 1;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    err = sqlite3_errmsg(db);
    sqlite3_close(db);
    return false;
  }

  int rc = sqlite3_step(st);
  bool ok = false;
  if (rc == SQLITE_ROW) {
    out.armed = sqlite3_column_int(st, 0) != 0;
    out.locked = sqlite3_column_int(st, 1) != 0;
    out.incubation = sqlite3_column_int(st, 2) != 0;
    const unsigned char* ex = sqlite3_column_text(st, 3);
    if (ex) ParseExeArray(reinterpret_cast<const char*>(ex), out.exes);
    out.ok = true;
    out.source = "sqlite";
    ok = true;
  } else {
    err = "no enforcer_runtime row";
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
  return ok;
}

bool LoadFromPolicyJson(const std::wstring& jsonPath, EnforcerSnapshot& out, std::string& err) {
  std::ifstream in(Narrow(jsonPath), std::ios::binary);
  if (!in) {
    err = "enforcer_policy.json not found";
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string body = ss.str();
  if (body.empty()) {
    err = "enforcer_policy.json empty";
    return false;
  }

  bool v = false;
  if (JsonBoolNear(body, "hard_block_armed", &v)) out.armed = v;
  if (JsonBoolNear(body, "gate_locked", &v)) out.locked = v;
  if (JsonBoolNear(body, "incubation_active", &v)) out.incubation = v;
  if (JsonBoolNear(body, "anti_tamper", &v)) out.anti_tamper = v;

  std::string s;
  if (JsonStringNear(body, "lock_mode", &s)) out.lock_mode = s;
  if (JsonStringNear(body, "unlock_password", &s)) out.unlock_password = s;
  if (JsonStringNear(body, "unlock_phrase", &s)) out.unlock_phrase = s;
  if (JsonStringNear(body, "provided_unlock", &s)) out.provided_unlock = s;

  std::int64_t n = 0;
  if (JsonIntNear(body, "lock_until_unix", &n)) out.lock_until_unix = n;

  size_t exesKey = body.find("\"exes\"");
  size_t exesJsonKey = body.find("\"exes_json\"");
  if (exesKey != std::string::npos) {
    size_t lb = body.find('[', exesKey);
    size_t rb = body.find(']', lb == std::string::npos ? 0 : lb);
    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
      ParseExeArray(body.substr(lb, rb - lb + 1), out.exes);
    }
  } else if (exesJsonKey != std::string::npos) {
    size_t lb = body.find('[', exesJsonKey);
    size_t rb = body.find(']', lb == std::string::npos ? 0 : lb);
    if (lb != std::string::npos && rb != std::string::npos) {
      ParseExeArray(body.substr(lb, rb - lb + 1), out.exes);
    }
  }

  ApplyStickyLock(out);

  out.ok = true;
  out.source = out.source.empty() ? "json" : "sqlite+json";
  return true;
}

}  // namespace

std::wstring DefaultPolicyJsonPath(const std::wstring& dbPath) {
  size_t slash = dbPath.find_last_of(L"\\/");
  std::wstring dataDir = (slash == std::wstring::npos) ? L"." : dbPath.substr(0, slash);
  return dataDir + L"\\behavior\\enforcer_policy.json";
}

bool SyncEnforcerRuntimeSqlite(const std::wstring& dbPath, const EnforcerSnapshot& snap) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(Narrow(dbPath).c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return false;
  }
  sqlite3_busy_timeout(db, 2000);
  std::ostringstream exes;
  exes << "[";
  for (size_t i = 0; i < snap.exes.size(); ++i) {
    if (i) exes << ", ";
    exes << "\"" << Narrow(snap.exes[i]) << "\"";
  }
  exes << "]";
  const std::string exesJson = exes.str();
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "UPDATE enforcer_runtime SET hard_block_armed=?, gate_locked=?, incubation_active=?, "
      "exes_json=?, note='synced_from_enforcer_gateway', updated_at=datetime('now') "
      "WHERE id=(SELECT id FROM enforcer_runtime ORDER BY id DESC LIMIT 1);";
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }
  sqlite3_bind_int(st, 1, snap.armed ? 1 : 0);
  sqlite3_bind_int(st, 2, snap.locked ? 1 : 0);
  sqlite3_bind_int(st, 3, snap.incubation ? 1 : 0);
  sqlite3_bind_text(st, 4, exesJson.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  sqlite3_close(db);
  return ok;
}

bool WriteEnforcerPolicyJson(const std::wstring& jsonPath, const EnforcerSnapshot& snap,
                             bool anti_tamper, bool protect_uninstall) {
  auto narrow = [](const std::wstring& w) -> std::string {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
  };
  size_t slash = jsonPath.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    CreateDirectoryW(jsonPath.substr(0, slash).c_str(), nullptr);
  }
  std::ostringstream o;
  o << "{\n"
    << "  \"hard_block_armed\": " << (snap.armed ? "true" : "false") << ",\n"
    << "  \"gate_locked\": " << (snap.locked ? "true" : "false") << ",\n"
    << "  \"incubation_active\": " << (snap.incubation ? "true" : "false") << ",\n"
    << "  \"exes\": [";
  for (size_t i = 0; i < snap.exes.size(); ++i) {
    if (i) o << ", ";
    o << "\"" << narrow(snap.exes[i]) << "\"";
  }
  o << "],\n"
    << "  \"note\": \"published_from_enforcer_gateway\",\n"
    << "  \"lock_mode\": \"" << snap.lock_mode << "\",\n"
    << "  \"lock_until_unix\": " << snap.lock_until_unix << ",\n"
    << "  \"unlock_password\": \"" << snap.unlock_password << "\",\n"
    << "  \"unlock_phrase\": \"" << snap.unlock_phrase << "\",\n"
    << "  \"anti_tamper\": " << (anti_tamper ? "true" : "false") << ",\n"
    << "  \"protect_uninstall\": " << (protect_uninstall ? "true" : "false") << "\n"
    << "}\n";
  std::string body = o.str();
  std::wstring tmp = jsonPath + L".tmp";
  {
    std::ofstream out(narrow(tmp), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(body.data(), (std::streamsize)body.size());
  }
  if (!MoveFileExW(tmp.c_str(), jsonPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::ofstream out(narrow(jsonPath), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(body.data(), (std::streamsize)body.size());
    DeleteFileW(tmp.c_str());
  }
  return true;
}

std::vector<std::wstring> AntiTamperExeList() {
  // CALT original list — Task Manager + process explorers + time tools.
  // Also shell hosts that can Unregister-ScheduledTask while Arm is locked
  // (default install is a user Scheduled Task — PowerShell was a one-line bypass).
  return {
      L"taskmgr.exe",
      L"procexp.exe",
      L"procexp64.exe",
      L"processhacker.exe",
      L"systeminformer.exe",
      L"resmon.exe",
      L"perfmon.exe",
      L"w32tm.exe",
      L"tzutil.exe",
      L"powershell.exe",
      L"powershell_ise.exe",
      L"pwsh.exe",
      L"windowsterminal.exe",
      L"wt.exe",
  };
}

bool LoadEnforcerSnapshot(const std::wstring& dbPath, EnforcerSnapshot& out, std::string& err) {
  out = EnforcerSnapshot{};
  std::string sqliteErr;
  bool fromDb = LoadFromSqlite(dbPath, out, sqliteErr);

  std::wstring jsonPath = DefaultPolicyJsonPath(dbPath);
  std::string jsonErr;
  EnforcerSnapshot fromJson;
  bool jsonOk = LoadFromPolicyJson(jsonPath, fromJson, jsonErr);

  if (jsonOk) {
    // JSON is authoritative when present — including empty exes (clears SQLite leftovers).
    out.armed = fromJson.armed;
    out.locked = fromJson.locked;
    out.incubation = fromJson.incubation;
    out.anti_tamper = fromJson.anti_tamper;
    out.lock_mode = fromJson.lock_mode;
    out.lock_until_unix = fromJson.lock_until_unix;
    out.unlock_password = fromJson.unlock_password;
    out.unlock_phrase = fromJson.unlock_phrase;
    out.provided_unlock = fromJson.provided_unlock;
    out.exes = fromJson.exes;
    out.ok = true;
    out.source = fromDb ? "sqlite+json" : "json";
    return true;
  }

  if (fromDb) return true;

  err = sqliteErr;
  if (!jsonErr.empty()) err += "; " + jsonErr;
  return false;
}
