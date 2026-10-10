#include "speech_host.h"

#include "wearable_json.h"

#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex gMu;
std::wstring gPiper;
std::wstring gPiperDir;
std::wstring gEspeak;
std::wstring gQwenModel;
std::wstring gNormalModel;
std::wstring gCacheDir;
bool gInstalled = false;
std::string gDetail = "Piper voice is not installed";
int gSeq = 0;

std::wstring EnvW(const wchar_t* name) {
  wchar_t buf[32768];
  DWORD n = GetEnvironmentVariableW(name, buf, 32768);
  if (n == 0 || n >= 32768) return L"";
  return buf;
}

bool FileExists(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

std::wstring DirOf(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return L".";
  return path.substr(0, slash);
}

std::string BaseName(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
  size_t dot = base.find(L".onnx");
  if (dot != std::wstring::npos) base.resize(dot);
  if (base.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, base.data(), (int)base.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return "";
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, base.data(), (int)base.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring FindPiper(const std::wstring& root) {
  std::wstring env = EnvW(L"CALT_PIPER");
  if (FileExists(env)) return env;
  const std::wstring dir = Join(root, L"tools\\piper");
  const std::wstring direct[] = {Join(dir, L"piper.exe"), Join(dir, L"piper")};
  for (const std::wstring& path : direct) {
    if (FileExists(path)) return path;
  }
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return L"";
  std::wstring found;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (fd.cFileName[0] == L'.') continue;
    const std::wstring nestedExe = Join(Join(dir, fd.cFileName), L"piper.exe");
    const std::wstring nested = Join(Join(dir, fd.cFileName), L"piper");
    if (FileExists(nestedExe)) {
      found = nestedExe;
      break;
    }
    if (FileExists(nested)) {
      found = nested;
      break;
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return found;
}

std::wstring FindVoice(const wchar_t* envName, const std::wstring& root, const wchar_t* file) {
  std::wstring env = EnvW(envName);
  if (FileExists(env)) return env;
  const std::wstring path = Join(Join(root, L"data\\models\\piper"), file);
  if (FileExists(path) && FileExists(path + L".json")) return path;
  return L"";
}

bool ReadFileCap(const std::wstring& path, std::string* out, size_t maxBytes) {
  out->clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 44 || (unsigned long long)sz.QuadPart > maxBytes) {
    CloseHandle(h);
    return false;
  }
  out->resize((size_t)sz.QuadPart);
  DWORD read = 0;
  BOOL ok = ReadFile(h, out->data(), (DWORD)out->size(), &read, nullptr);
  CloseHandle(h);
  return ok && read == out->size() && out->size() >= 12 && out->compare(0, 4, "RIFF") == 0;
}

std::string CleanText(const std::string& in) {
  std::string o;
  o.reserve(in.size());
  bool space = false;
  for (unsigned char c : in) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (c < 32) continue;
    if (c == ' ') {
      if (space || o.empty()) continue;
      space = true;
    } else {
      space = false;
    }
    o.push_back((char)c);
    if (o.size() >= 480) break;
  }
  while (!o.empty() && o.back() == ' ') o.pop_back();
  return o;
}

bool RunPiper(const std::wstring& model, const wchar_t* lengthScale, const std::string& text,
              std::string* wav, std::string* err) {
  wav->clear();
  std::wstring cache = gCacheDir;
  CreateDirectoryW(DirOf(cache).c_str(), nullptr);
  if (!CreateDirectoryW(cache.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS &&
      !DirExists(cache)) {
    cache = DirOf(model);
  }
  const std::wstring outPath = Join(cache, L"speak-" + std::to_wstring(++gSeq) + L".wav");
  DeleteFileW(outPath.c_str());

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE inRead = nullptr;
  HANDLE inWrite = nullptr;
  if (!CreatePipe(&inRead, &inWrite, &sa, 0)) {
    *err = "speech pipe failed";
    return false;
  }
  SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

  std::wstring cmd = Quote(gPiper) + L" --model " + Quote(model) + L" --output_file " + Quote(outPath) +
                     L" --length_scale " + lengthScale + L" --sentence_silence 0.15";
  if (!gEspeak.empty()) cmd += L" --espeak_data " + Quote(gEspeak);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdInput = inRead;
  si.hStdOutput = nul == INVALID_HANDLE_VALUE ? nullptr : nul;
  si.hStdError = nul == INVALID_HANDLE_VALUE ? nullptr : nul;
  PROCESS_INFORMATION pi{};
  const BOOL started =
      CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                     gPiperDir.c_str(), &si, &pi);
  CloseHandle(inRead);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  if (!started) {
    CloseHandle(inWrite);
    *err = "piper did not start";
    return false;
  }
  DWORD written = 0;
  std::string payload = text + "\n";
  WriteFile(inWrite, payload.data(), (DWORD)payload.size(), &written, nullptr);
  CloseHandle(inWrite);
  const DWORD wait = WaitForSingleObject(pi.hProcess, 20000);
  if (wait != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (wait != WAIT_OBJECT_0 || code != 0 || !ReadFileCap(outPath, wav, 8 * 1024 * 1024)) {
    DeleteFileW(outPath.c_str());
    *err = "piper did not write audio";
    return false;
  }
  DeleteFileW(outPath.c_str());
  return true;
}

std::string ChildStr(const Wj& obj, const char* key) {
  const Wj* v = WjGet(obj, key);
  std::string s;
  if (v && WjAsString(*v, &s)) return s;
  return "";
}

}  // namespace

void SpeechPrepare(const std::wstring& repoRoot) {
  std::lock_guard<std::mutex> lock(gMu);
  gInstalled = false;
  gDetail = "Piper voice is not installed";
  gPiper.clear();
  gQwenModel.clear();
  gNormalModel.clear();
  gEspeak.clear();
  if (EnvW(L"CALT_TTS_DISABLE") == L"1") {
    gDetail = "Speech is turned off (CALT_TTS_DISABLE=1)";
    return;
  }
  gPiper = FindPiper(repoRoot);
  gQwenModel = FindVoice(L"CALT_TTS_VOICE_QWEN", repoRoot, L"en_GB-alan-medium.onnx");
  gNormalModel = FindVoice(L"CALT_TTS_VOICE_NORMAL", repoRoot, L"en_US-lessac-medium.onnx");
  if (gNormalModel.empty()) gNormalModel = gQwenModel;
  if (gQwenModel.empty()) gQwenModel = gNormalModel;
  gCacheDir = Join(Join(repoRoot, L"data\\models\\piper"), L"cache");
  if (gPiper.empty() || gQwenModel.empty()) {
    gDetail = "Run scripts\\run\\fetch_piper.bat (Piper exe plus the Alan and Lessac voices)";
    return;
  }
  gPiperDir = DirOf(gPiper);
  const std::wstring espeak = Join(gPiperDir, L"espeak-ng-data");
  if (DirExists(espeak)) gEspeak = espeak;
  gInstalled = true;
  gDetail = "Piper ready";
}

SpeechSnap SpeechStatus() {
  std::lock_guard<std::mutex> lock(gMu);
  SpeechSnap s;
  s.installed = gInstalled;
  s.ready = gInstalled;
  s.qwenVoice = BaseName(gQwenModel);
  s.normalVoice = BaseName(gNormalModel);
  s.detail = gDetail;
  return s;
}

SpeechReply SpeechHandle(const std::string& method, const std::string& path, const std::string& body) {
  std::string p = path;
  if (p.size() > 1 && p.back() == '/') p.pop_back();
  const bool status = method == "GET" && (p == "/api/speech" || p == "/api/speech/status" || p == "/api/tts" ||
                                           p == "/api/tts/status");
  if (status) {
    SpeechSnap s = SpeechStatus();
    Wj o;
    o.type = Wj::kObj;
    WjSet(o, "ok", WjBool(true));
    WjSet(o, "ready", WjBool(s.ready));
    WjSet(o, "installed", WjBool(s.installed));
    WjSet(o, "engine", WjStr("piper"));
    WjSet(o, "qwen_voice", s.qwenVoice.empty() ? WjNull() : WjStr(s.qwenVoice));
    WjSet(o, "normal_voice", s.normalVoice.empty() ? WjNull() : WjStr(s.normalVoice));
    WjSet(o, "detail", WjStr(s.detail));
    return SpeechReply{200, "application/json", WjStringify(o)};
  }
  if (!(method == "POST" && (p == "/api/speech" || p == "/api/tts"))) {
    return SpeechReply{404, "application/json", "{\"ok\":false,\"detail\":\"unknown speech route\"}"};
  }
  Wj req;
  std::string perr;
  if (!WjParse(body, &req, &perr) || req.type != Wj::kObj) {
    return SpeechReply{400, "application/json", "{\"ok\":false,\"detail\":\"invalid JSON\"}"};
  }
  std::string text = CleanText(ChildStr(req, "text"));
  if (text.empty()) return SpeechReply{400, "application/json", "{\"ok\":false,\"detail\":\"text required\"}"};
  std::string voice = ChildStr(req, "voice");
  if (voice == "jarvis") voice = "qwen";
  const bool normal = voice == "normal";

  std::wstring model;
  const wchar_t* scale = L"1.05";
  {
    std::lock_guard<std::mutex> lock(gMu);
    if (!gInstalled) {
      Wj o;
      o.type = Wj::kObj;
      WjSet(o, "ok", WjBool(false));
      WjSet(o, "ready", WjBool(false));
      WjSet(o, "detail", WjStr(gDetail));
      return SpeechReply{503, "application/json", WjStringify(o)};
    }
    model = normal ? gNormalModel : gQwenModel;
    if (normal) scale = L"1.0";
  }
  std::string wav;
  std::string err;
  std::lock_guard<std::mutex> lock(gMu);
  if (!RunPiper(model, scale, text, &wav, &err)) {
    Wj o;
    o.type = Wj::kObj;
    WjSet(o, "ok", WjBool(false));
    WjSet(o, "detail", WjStr(err.empty() ? "speech failed" : err));
    return SpeechReply{503, "application/json", WjStringify(o)};
  }
  return SpeechReply{200, "audio/wav", std::move(wav)};
}
