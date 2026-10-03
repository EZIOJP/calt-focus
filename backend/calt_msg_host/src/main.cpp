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
      WriteMessage(R"({"type":"pong","ok":true,"host":"calt_msg_host","softland":true,"track":true})");
      continue;
    }
    if (HasType(msg, "get_mode")) {
      std::string url = ExtractJsonString(msg, "url");
      std::string now = ExtractJsonString(msg, "now");
      SoftlandModeResult r = SoftlandGetMode(url, now);
      WriteMessage(SoftlandModeResultToJson(r));
      continue;
    }
    if (HasType(msg, "get_softland_doc")) {
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
      WriteMessage(ok ? R"({"type":"track_tab","ok":true})"
                      : R"({"type":"track_tab","ok":false,"error":"db_write_failed"})");
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
