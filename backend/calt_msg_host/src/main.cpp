/**
 * CALT Native Messaging Host — SoftLand get_mode + track_tab (solo pack Phase 1).
 * Chrome/Edge: 4-byte LE length + UTF-8 JSON on stdin/stdout.
 */

#include "softland_decide.h"
#include "track_tab.h"

#include <cstdint>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace {

bool ReadMessage(std::string& out) {
  std::uint32_t len = 0;
  if (std::fread(&len, 4, 1, stdin) != 1) return false;
  if (len == 0 || len > 1024 * 1024) return false;
  out.assign(len, '\0');
  return std::fread(out.data(), 1, len, stdin) == len;
}

void WriteMessage(const std::string& msg) {
  std::uint32_t len = static_cast<std::uint32_t>(msg.size());
  std::fwrite(&len, 4, 1, stdout);
  std::fwrite(msg.data(), 1, len, stdout);
  std::fflush(stdout);
}

bool HasType(const std::string& msg, const char* type) {
  std::string a = std::string("\"type\":\"") + type + "\"";
  std::string b = std::string("\"type\": \"") + type + "\"";
  return msg.find(a) != std::string::npos || msg.find(b) != std::string::npos;
}

std::string ExtractJsonString(const std::string& msg, const char* key) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = msg.find(needle);
  if (p == std::string::npos) return {};
  size_t colon = msg.find(':', p + needle.size());
  if (colon == std::string::npos) return {};
  size_t i = colon + 1;
  while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
  if (i < msg.size() && msg[i] == 'n') return {};
  if (i >= msg.size() || msg[i] != '"') return {};
  ++i;
  std::string tok;
  while (i < msg.size() && msg[i] != '"') {
    if (msg[i] == '\\' && i + 1 < msg.size()) {
      tok.push_back(msg[i + 1]);
      i += 2;
      continue;
    }
    tok.push_back(msg[i++]);
  }
  return tok;
}

}  // namespace

int main() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  std::string msg;
  while (ReadMessage(msg)) {
    if (HasType(msg, "ping")) {
      TouchGateHeartbeat("ping");
      WriteMessage(R"({"type":"pong","ok":true,"host":"calt_msg_host","softland":true,"track":true})");
      continue;
    }
    if (HasType(msg, "get_mode")) {
      std::string url = ExtractJsonString(msg, "url");
      std::string now = ExtractJsonString(msg, "now");
      SoftlandModeResult r = SoftlandGetMode(url, now);
      TouchGateHeartbeat("get_mode");
      WriteMessage(SoftlandModeResultToJson(r));
      continue;
    }
    if (HasType(msg, "get_softland_doc")) {
      TouchGateHeartbeat("get_softland_doc");
      WriteMessage(SoftlandGateSnapshotJson());
      continue;
    }
    if (HasType(msg, "track_tab") || HasType(msg, "track_heartbeat")) {
      std::string url = ExtractJsonString(msg, "url");
      std::string title = ExtractJsonString(msg, "title");
      std::string domain = ExtractJsonString(msg, "domain");
      if (domain.empty() && !url.empty()) {
        size_t p = url.find("://");
        size_t start = p == std::string::npos ? 0 : p + 3;
        size_t end = url.find('/', start);
        domain = end == std::string::npos ? url.substr(start) : url.substr(start, end - start);
      }
      bool ok = TrackTabToSqlite(url, title, domain);
      if (ok) TouchGateHeartbeat(HasType(msg, "track_heartbeat") ? "track_heartbeat" : "track_tab");
      WriteMessage(ok ? R"({"type":"track_tab","ok":true})"
                      : R"({"type":"track_tab","ok":false,"error":"db_write_failed"})");
      continue;
    }
    if (HasType(msg, "set_quinn_prefs") || HasType(msg, "get_quinn_prefs")) {
      std::wstring policy = SoftlandPolicyPathW();
      size_t slash = policy.find_last_of(L"\\/");
      std::wstring dir = (slash == std::wstring::npos) ? L"." : policy.substr(0, slash);
      CreateDirectoryW(dir.c_str(), nullptr);
      std::wstring path = dir + L"\\quinn_dashboard.json";
      if (HasType(msg, "get_quinn_prefs")) {
        FILE* rf = nullptr;
#if defined(_MSC_VER)
        _wfopen_s(&rf, path.c_str(), L"rb");
#else
        rf = _wfopen(path.c_str(), L"rb");
#endif
        if (!rf) {
          WriteMessage(
              R"({"type":"get_quinn_prefs","ok":true,"appeals_enabled":true,"source":"default"})");
          continue;
        }
        std::string body;
        char buf[512];
        while (size_t n = fread(buf, 1, sizeof(buf), rf)) body.append(buf, n);
        fclose(rf);
        if (body.empty()) {
          WriteMessage(
              R"({"type":"get_quinn_prefs","ok":true,"appeals_enabled":true,"source":"empty"})");
        } else {
          bool en = body.find("\"appeals_enabled\":false") == std::string::npos &&
                    body.find("\"appeals_enabled\": false") == std::string::npos;
          WriteMessage(std::string("{\"type\":\"get_quinn_prefs\",\"ok\":true,\"source\":\"file\",") +
                       "\"appeals_enabled\":" + (en ? "true" : "false") + ",\"document\":" + body +
                       "}");
        }
        continue;
      }
      // set_quinn_prefs — compact dashboard mirror for Focus / Gate.
      bool en = msg.find("\"appeals_enabled\":false") == std::string::npos &&
                msg.find("\"appeals_enabled\": false") == std::string::npos;
      SYSTEMTIME st;
      GetSystemTime(&st);
      char iso[40];
      snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
      std::string out = std::string("{\"appeals_enabled\":") + (en ? "true" : "false") +
                        ",\"updated_at\":\"" + iso + "\",\"source\":\"gate\"}\n";
      std::wstring tmp = dir + L"\\quinn_dashboard.json.tmp";
      FILE* f = nullptr;
#if defined(_MSC_VER)
      _wfopen_s(&f, tmp.c_str(), L"wb");
#else
      f = _wfopen(tmp.c_str(), L"wb");
#endif
      if (!f) {
        WriteMessage(R"({"type":"set_quinn_prefs","ok":false,"error":"write_failed"})");
        continue;
      }
      fwrite(out.data(), 1, out.size(), f);
      fclose(f);
      MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
      WriteMessage(std::string("{\"type\":\"set_quinn_prefs\",\"ok\":true,\"appeals_enabled\":") +
                   (en ? "true" : "false") + "}");
      continue;
    }
    if (HasType(msg, "log_appeal")) {
      // Append one JSON line for Focus / Quinn appeal trail (best-effort).
      std::wstring policy = SoftlandPolicyPathW();
      size_t slash = policy.find_last_of(L"\\/");
      std::wstring dir = (slash == std::wstring::npos) ? L"." : policy.substr(0, slash);
      CreateDirectoryW(dir.c_str(), nullptr);
      std::wstring path = dir + L"\\porn_appeals.jsonl";
      FILE* f = nullptr;
#if defined(_MSC_VER)
      _wfopen_s(&f, path.c_str(), L"ab");
#else
      f = _wfopen(path.c_str(), L"ab");
#endif
      if (f) {
        fwrite(msg.data(), 1, msg.size(), f);
        fputc('\n', f);
        fclose(f);
        WriteMessage(R"({"type":"log_appeal","ok":true})");
      } else {
        WriteMessage(R"({"type":"log_appeal","ok":false,"error":"write_failed"})");
      }
      continue;
    }
    if (HasType(msg, "softland_path")) {
      std::wstring p = SoftlandPolicyPathW();
      std::string narrow;
      int n = WideCharToMultiByte(CP_UTF8, 0, p.c_str(), (int)p.size(), nullptr, 0, nullptr, nullptr);
      narrow.assign(n, '\0');
      WideCharToMultiByte(CP_UTF8, 0, p.c_str(), (int)p.size(), narrow.data(), n, nullptr, nullptr);
      std::string esc;
      for (char c : narrow) {
        if (c == '\\') esc += "\\\\";
        else if (c == '"') esc += "\\\"";
        else esc.push_back(c);
      }
      WriteMessage(std::string("{\"ok\":true,\"path\":\"") + esc + "\"}");
      continue;
    }
    WriteMessage(R"({"type":"error","ok":false,"error":"unknown_message"})");
  }
  return 0;
}
