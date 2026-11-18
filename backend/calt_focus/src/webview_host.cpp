#include "webview_host.h"
#include "enforcer_cmd.h"
#include "llm_http_proxy.h"
#include "openjarvis_ctrl.h"
#include "paths.h"
#include "pc_apps.h"
#include "pc_power.h"
#include "sync_host_ctrl.h"
#include "telegram_remote.h"

#include <WebView2.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shell32.lib")

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace {

/** Focus WebView may only load CALT virtual hosts — never arbitrary web (drag-drop / window.open bypass). */
bool IsAllowedFocusUri(const wchar_t* uri) {
  if (!uri || !uri[0]) {
    return false;
  }
  if (_wcsnicmp(uri, L"about:", 6) == 0) {
    return true;
  }
  static const wchar_t* kHosts[] = {
      L"calt.app",
      L"calt-learn.app",
      L"calt-data.app",
      L"calt-bible.app",
  };
  for (const wchar_t* host : kHosts) {
    std::wstring prefix = L"https://";
    prefix += host;
    const size_t n = prefix.size();
    if (_wcsnicmp(uri, prefix.c_str(), static_cast<int>(n)) != 0) {
      continue;
    }
    const wchar_t c = uri[n];
    if (c == L'\0' || c == L'/' || c == L'?' || c == L'#') {
      return true;
    }
  }
  return false;
}

void FreeWvStr(LPWSTR s) {
  if (s) {
    CoTaskMemFree(s);
  }
}

class EnvHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
 public:
  explicit EnvHandler(std::function<void(HRESULT, ICoreWebView2Environment*)> cb)
      : cb_(std::move(cb)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
      *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG n = --ref_;
    if (n == 0) {
      delete this;
    }
    return n;
  }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,
                                   ICoreWebView2Environment* createdEnvironment) override {
    cb_(errorCode, createdEnvironment);
    return S_OK;
  }

 private:
  std::function<void(HRESULT, ICoreWebView2Environment*)> cb_;
  std::atomic<ULONG> ref_{1};
};

class CtrlHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
 public:
  explicit CtrlHandler(std::function<void(HRESULT, ICoreWebView2Controller*)> cb)
      : cb_(std::move(cb)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
      *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG n = --ref_;
    if (n == 0) {
      delete this;
    }
    return n;
  }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,
                                   ICoreWebView2Controller* createdController) override {
    cb_(errorCode, createdController);
    return S_OK;
  }

 private:
  std::function<void(HRESULT, ICoreWebView2Controller*)> cb_;
  std::atomic<ULONG> ref_{1};
};

using CreateEnvFn = HRESULT(STDMETHODCALLTYPE*)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);

HMODULE LoadWebView2Loader() {
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  PathRemoveFileSpecW(exe);
  const std::wstring beside = std::wstring(exe) + L"\\WebView2Loader.dll";
  if (HMODULE m = LoadLibraryW(beside.c_str())) {
    return m;
  }
  return LoadLibraryW(L"WebView2Loader.dll");
}

std::wstring WidenUtf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
  return w;
}

std::string NarrowUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::string JsonEscapeUtf8(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        o += "\\\"";
        break;
      case '\\':
        o += "\\\\";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      case '\t':
        o += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += (char)c;
        }
    }
  }
  return o;
}

bool GrabJsonQuoted(const std::string& msg, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = msg.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = msg.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
  if (i >= msg.size() || msg[i] != '"') return false;
  ++i;
  out->clear();
  while (i < msg.size() && msg[i] != '"') {
    if (msg[i] == '\\' && i + 1 < msg.size()) {
      const char n = msg[i + 1];
      if (n == 'n')
        out->push_back('\n');
      else if (n == 'r')
        out->push_back('\r');
      else if (n == 't')
        out->push_back('\t');
      else if (n == 'u' && i + 5 < msg.size()) {
        // Keep as-is for rare unicode escapes in LLM payloads — pass through bytes.
        out->push_back('\\');
        out->push_back('u');
        out->push_back(msg[i + 2]);
        out->push_back(msg[i + 3]);
        out->push_back(msg[i + 4]);
        out->push_back(msg[i + 5]);
        i += 6;
        continue;
      } else
        out->push_back(n);
      i += 2;
      continue;
    }
    out->push_back(msg[i++]);
  }
  return true;
}

bool GrabJsonInt(const std::string& msg, const char* key, int* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = msg.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = msg.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
  if (i >= msg.size()) return false;
  *out = atoi(msg.c_str() + i);
  return true;
}

bool GrabJsonBool(const std::string& msg, const char* key, bool* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = msg.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = msg.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
  if (msg.compare(i, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (msg.compare(i, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

/** One enforcer pipe client at a time — 2 server instances + unbounded
 *  detached threads left the pipe permanently ERROR_PIPE_BUSY (calendar
 *  then showed only FE sleep, no desktop sessions). */
struct EnforcerCmdJob {
  ICoreWebView2* sender = nullptr;
  std::string req;
  std::string id;
  std::string op;
  unsigned timeoutMs = 5000;
};

/** Heap payload for UI-thread PostWebMessageAsJson (worker must not call COM). */
struct EnforcerCmdUiMsg {
  ICoreWebView2* sender = nullptr;
  std::wstring json;
};

HWND gEnforcerUiHwnd = nullptr;

void PostEnforcerResultToUi(ICoreWebView2* sender, std::wstring json) {
  if (!sender) return;
  auto* msg = new EnforcerCmdUiMsg{sender, std::move(json)};
  HWND hwnd = gEnforcerUiHwnd;
  if (hwnd && PostMessageW(hwnd, WebViewHost::WM_ENFORCER_CMD_RESULT, 0,
                           reinterpret_cast<LPARAM>(msg))) {
    return;
  }
  // Fallback (UI hwnd missing): best-effort on this thread.
  sender->PostWebMessageAsJson(msg->json.c_str());
  sender->Release();
  delete msg;
}

class EnforcerCmdQueue {
 public:
  static EnforcerCmdQueue& Instance() {
    static EnforcerCmdQueue q;
    return q;
  }

  void Enqueue(ICoreWebView2* sender, std::string req, std::string id, std::string op,
               unsigned timeoutMs) {
    if (!sender) return;
    sender->AddRef();
    if (timeoutMs < 800) timeoutMs = 800;
    if (timeoutMs > 30000) timeoutMs = 30000;
    {
      std::lock_guard<std::mutex> lock(mu_);
      // Drop oldest queued probes if backlog grows (keep UI writes moving).
      // Never drop plan.overlay — calendar desktop actuals depend on it.
      while (jobs_.size() >= 6) {
        auto it = jobs_.begin();
        while (it != jobs_.end() && it->op == "plan.overlay") ++it;
        if (it == jobs_.end()) break;  // only overlays queued
        EnforcerCmdJob stale = std::move(*it);
        jobs_.erase(it);
        const std::string out =
            std::string("{\"type\":\"enforcer_cmd_result\",\"ok\":false,\"id\":\"") +
            stale.id + "\",\"error\":\"queue_dropped\"}";
        // Enqueue runs on the WebView message thread — safe to post directly.
        const std::wstring w = WidenUtf8(out);
        stale.sender->PostWebMessageAsJson(w.c_str());
        stale.sender->Release();
      }
      EnforcerCmdJob job{sender, std::move(req), std::move(id), std::move(op), timeoutMs};
      // User writes jump ahead of status probes (Bible assign was starving behind polls).
      const bool priority =
          job.op == "plan.overlay" || job.op.rfind("bible.", 0) == 0 ||
          job.op.rfind("softland.", 0) == 0 || job.op.rfind("arm.", 0) == 0 ||
          job.op.rfind("day.confirm", 0) == 0;
      if (priority) {
        auto it = jobs_.begin();
        while (it != jobs_.end() && it->op == "plan.overlay") ++it;
        jobs_.insert(it, std::move(job));
      } else {
        jobs_.push_back(std::move(job));
      }
    }
    cv_.notify_one();
    EnsureWorker();
  }

 private:
  EnforcerCmdQueue() = default;

  void EnsureWorker() {
    bool expected = false;
    if (!worker_started_.compare_exchange_strong(expected, true)) return;
    std::thread([this]() { WorkerLoop(); }).detach();
  }

  void WorkerLoop() {
    for (;;) {
      EnforcerCmdJob job;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&]() { return !jobs_.empty(); });
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      std::string resp;
      std::string out;
      // plan.overlay can be 50–100KB+; a hard 4s cap made FE fall back to sleep-only.
      unsigned timeoutMs = job.timeoutMs;
      if (job.op == "plan.overlay" && timeoutMs < 12000) timeoutMs = 12000;
      if (job.op.rfind("bible.devotion.", 0) == 0 && timeoutMs < 15000) timeoutMs = 15000;
      if (!EnforcerSendCommand(job.req, resp, timeoutMs)) {
        out = std::string("{\"type\":\"enforcer_cmd_result\",\"ok\":false,\"id\":\"") +
              job.id + "\",\"error\":\"enforcer_unreachable\"}";
      } else if (!resp.empty() && resp[0] == '{') {
        out = std::string("{\"type\":\"enforcer_cmd_result\",") + resp.substr(1);
      } else {
        out = std::string("{\"type\":\"enforcer_cmd_result\",\"ok\":false,\"id\":\"") +
              job.id + "\",\"error\":\"bad_response\"}";
      }
      // WebView2 COM must run on the UI thread — posting from this worker was
      // silently dropping results so the calendar FE timed out after send_ok.
      PostEnforcerResultToUi(job.sender, WidenUtf8(out));
    }
  }

  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<EnforcerCmdJob> jobs_;
  std::atomic<bool> worker_started_{false};
};

/** Block drag-drop / typed navigations to the open web — stay on CALT virtual hosts only. */
class NavStartingHandler : public ICoreWebView2NavigationStartingEventHandler {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ICoreWebView2NavigationStartingEventHandler) {
      *ppv = static_cast<ICoreWebView2NavigationStartingEventHandler*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG n = --ref_;
    if (n == 0) delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* /*sender*/,
                                   ICoreWebView2NavigationStartingEventArgs* args) override {
    if (!args) return S_OK;
    LPWSTR uri = nullptr;
    if (FAILED(args->get_Uri(&uri)) || !uri) return S_OK;
    if (!IsAllowedFocusUri(uri)) {
      args->put_Cancel(TRUE);
    }
    FreeWvStr(uri);
    return S_OK;
  }

 private:
  std::atomic<ULONG> ref_{1};
};

/** Never spawn an external Edge/Chrome window from Focus (window.open, target=_blank, some drops). */
class NewWindowHandler : public ICoreWebView2NewWindowRequestedEventHandler {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ICoreWebView2NewWindowRequestedEventHandler) {
      *ppv = static_cast<ICoreWebView2NewWindowRequestedEventHandler*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG n = --ref_;
    if (n == 0) delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                   ICoreWebView2NewWindowRequestedEventArgs* args) override {
    if (!args) return S_OK;
    // Always consume — never let WebView2 open a second window outside Gate.
    args->put_Handled(TRUE);
    LPWSTR uri = nullptr;
    if (FAILED(args->get_Uri(&uri)) || !uri) return S_OK;
    if (sender && IsAllowedFocusUri(uri)) {
      sender->Navigate(uri);
    }
    FreeWvStr(uri);
    return S_OK;
  }

 private:
  std::atomic<ULONG> ref_{1};
};

class MsgHandler : public ICoreWebView2WebMessageReceivedEventHandler {
 public:
  explicit MsgHandler(HWND shellHwnd) : shell_hwnd_(shellHwnd) {
    if (shellHwnd) gEnforcerUiHwnd = shellHwnd;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
      *ppv = static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG n = --ref_;
    if (n == 0) delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                   ICoreWebView2WebMessageReceivedEventArgs* args) override {
    if (!sender || !args) return S_OK;
    LPWSTR json = nullptr;
    if (FAILED(args->get_WebMessageAsJson(&json)) || !json) return S_OK;
    std::string msg = NarrowUtf8(json);
    CoTaskMemFree(json);

    // Debug-mode NDJSON append (FE agentDebugLog dual-write).
    if (msg.find("\"agent_debug_log\"") != std::string::npos) {
      std::string line;
      {
        std::string needle = "\"line\"";
        size_t p = msg.find(needle);
        if (p != std::string::npos) {
          size_t colon = msg.find(':', p + needle.size());
          if (colon != std::string::npos) {
            size_t i = colon + 1;
            while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
            if (i < msg.size() && msg[i] == '"') {
              ++i;
              while (i < msg.size() && msg[i] != '"') {
                if (msg[i] == '\\' && i + 1 < msg.size()) {
                  line.push_back(msg[i + 1]);
                  i += 2;
                  continue;
                }
                line.push_back(msg[i++]);
              }
            }
          }
        }
      }
      if (!line.empty()) {
        const std::wstring dir = RepoRoot() + L"\\.cursor";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring path = dir + L"\\debug-56e50d.log";
        std::ofstream out(path.c_str(), std::ios::app | std::ios::binary);
        if (out) {
          out << line << "\n";
        }
      }
      return S_OK;
    }

    // In-app Focus menu → native shell (reload / tray / quit) when tray is hard to find.
    if (msg.find("\"focus_shell\"") != std::string::npos && shell_hwnd_) {
      std::string action;
      auto grabQuoted = [&](const char* key, std::string* out) {
        std::string needle = std::string("\"") + key + "\"";
        size_t p = msg.find(needle);
        if (p == std::string::npos) return;
        size_t colon = msg.find(':', p + needle.size());
        if (colon == std::string::npos) return;
        size_t i = colon + 1;
        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
        if (i >= msg.size() || msg[i] != '"') return;
        ++i;
        while (i < msg.size() && msg[i] != '"') {
          if (msg[i] == '\\' && i + 1 < msg.size()) {
            out->push_back(msg[i + 1]);
            i += 2;
            continue;
          }
          out->push_back(msg[i++]);
        }
      };
      grabQuoted("action", &action);
      // Must match FocusApp WM_FOCUS_SHELL WPARAM values in app.cpp
      constexpr UINT WM_FOCUS_SHELL = WM_APP + 55;
      WPARAM wp = 0;
      if (action == "reload_ui") wp = 1;
      else if (action == "ensure_tray") wp = 2;
      else if (action == "quit") wp = 3;
      else if (action == "open_settings") wp = 4;
      else if (action == "popup_tray") wp = 5;
      else if (action == "restart_enforcer") wp = 6;
      else if (action == "open_roadmap") wp = 8;
      else if (action == "open_openjarvis") wp = 9;
      else if (action == "update_ui") wp = 10;
      else if (action == "update_stack") wp = 11;
      else if (action == "apply_update") wp = 12;
      else if (action == "run_stack" || action == "ensure_enforcer") wp = 13;
      else if (action == "start_api") wp = 14;
      else if (action == "start_frontend") wp = 15;
      else if (action == "start_web_stack") wp = 16;
      else if (action == "sleeper_on") wp = 17;
      else if (action == "sleeper_off") wp = 18;
      else if (action == "sleeper_soft") wp = 19;
      if (wp) PostMessageW(shell_hwnd_, WM_FOCUS_SHELL, wp, 0);
      return S_OK;
    }

    // Whitelisted behavior-folder files — export download bypasses fragile calt-data fetch.
    // {"type":"behavior_file","id":"...","op":"copy_downloads|reveal|open","file":"productivity_export.json",
    //  "download_name":"productivity-7d-20261107.json"}
    if (msg.find("\"behavior_file\"") != std::string::npos) {
      std::string id, op, file, downloadName;
      GrabJsonQuoted(msg, "id", &id);
      GrabJsonQuoted(msg, "op", &op);
      GrabJsonQuoted(msg, "file", &file);
      GrabJsonQuoted(msg, "download_name", &downloadName);
      auto reply = [&](bool ok, const std::string& detail, const std::string& pathUtf8 = "") {
        std::string body = std::string("{\"type\":\"behavior_file_result\",\"id\":\"") +
                           JsonEscapeUtf8(id) + "\",\"ok\":" + (ok ? "true" : "false") +
                           ",\"detail\":\"" + JsonEscapeUtf8(detail) + "\"";
        if (!pathUtf8.empty())
          body += std::string(",\"path\":\"") + JsonEscapeUtf8(pathUtf8) + "\"";
        body += "}";
        sender->PostWebMessageAsJson(WidenUtf8(body).c_str());
      };
      auto allowed = [](const std::string& name) {
        return name == "productivity_export.json" || name == "productivity_export.csv" ||
               name == "softland_policy.json" || name == "pending_update.json";
      };
      if (file.empty() || file.find("..") != std::string::npos || file.find('/') != std::string::npos ||
          file.find('\\') != std::string::npos || !allowed(file)) {
        reply(false, "bad_file");
        return S_OK;
      }
      const std::wstring src = DataBehaviorDir() + L"\\" + WidenUtf8(file);
      if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        reply(false, "missing");
        return S_OK;
      }
      if (op == "reveal") {
        std::wstring params = L"/select,\"";
        params += src;
        params += L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
        reply(true, "revealed");
        return S_OK;
      }
      if (op == "open") {
        ShellExecuteW(nullptr, L"open", src.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        reply(true, "opened");
        return S_OK;
      }
      if (op == "copy_downloads") {
        std::wstring destName = WidenUtf8(downloadName.empty() ? file : downloadName);
        for (wchar_t& c : destName) {
          if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' || c == L'"' ||
              c == L'<' || c == L'>' || c == L'|')
            c = L'_';
        }
        if (destName.empty()) destName = WidenUtf8(file);
        PWSTR downloads = nullptr;
        std::wstring destDir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads)) &&
            downloads) {
          destDir = downloads;
          CoTaskMemFree(downloads);
        } else {
          wchar_t profile[MAX_PATH]{};
          if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
            destDir = std::wstring(profile) + L"\\Downloads";
          }
        }
        if (destDir.empty()) {
          reply(false, "no_downloads");
          return S_OK;
        }
        CreateDirectoryW(destDir.c_str(), nullptr);
        const std::wstring dest = destDir + L"\\" + destName;
        if (!CopyFileW(src.c_str(), dest.c_str(), FALSE)) {
          reply(false, "copy_failed");
          return S_OK;
        }
        ShellExecuteW(nullptr, L"open", dest.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        // Narrow path for UI hint (lossy for non-ASCII is ok).
        std::string pathUtf8;
        for (wchar_t c : dest) {
          if (c >= 32 && c < 127) pathUtf8.push_back(static_cast<char>(c));
          else pathUtf8.push_back('?');
        }
        reply(true, "copied", pathUtf8);
        return S_OK;
      }
      reply(false, "bad_op");
      return S_OK;
    }

    // Open Study / external browser links from Focus UI.
    if (msg.find("\"open_external\"") != std::string::npos) {
      std::string url;
      auto grabUrl = [&]() {
        std::string needle = "\"url\"";
        size_t p = msg.find(needle);
        if (p == std::string::npos) return;
        size_t colon = msg.find(':', p + needle.size());
        if (colon == std::string::npos) return;
        size_t i = colon + 1;
        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
        if (i >= msg.size() || msg[i] != '"') return;
        ++i;
        while (i < msg.size() && msg[i] != '"') {
          if (msg[i] == '\\' && i + 1 < msg.size()) {
            url.push_back(msg[i + 1]);
            i += 2;
            continue;
          }
          url.push_back(msg[i++]);
        }
      };
      grabUrl();
      if (!url.empty()) {
        std::wstring wurl = WidenUtf8(url);
        // Never ShellExecute arbitrary web from Focus — Gate/SoftLand bypass.
        if (IsAllowedFocusUri(wurl.c_str())) {
          ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
      }
      return S_OK;
    }

    // Whitelisted Admin scripts under scripts\ (device hosts lock).
    if (msg.find("\"run_repo_bat\"") != std::string::npos) {
      std::string name;
      std::string confirm;
      auto grabQuoted = [&](const char* key, std::string* out) {
        std::string needle = std::string("\"") + key + "\"";
        size_t p = msg.find(needle);
        if (p == std::string::npos) return;
        size_t colon = msg.find(':', p + needle.size());
        if (colon == std::string::npos) return;
        size_t i = colon + 1;
        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
        if (i >= msg.size() || msg[i] != '"') return;
        ++i;
        while (i < msg.size() && msg[i] != '"') {
          if (msg[i] == '\\' && i + 1 < msg.size()) {
            out->push_back(msg[i + 1]);
            i += 2;
            continue;
          }
          out->push_back(msg[i++]);
        }
      };
      grabQuoted("name", &name);
      grabQuoted("confirm", &confirm);
      if (name == "device_block_apply.bat" || name == "device_block_remove.bat" ||
          name == "run\\pull_zeppbridge.bat" || name == "pull_zeppbridge.bat") {
        std::wstring rel = WidenUtf8(name);
        // Allow scripts\run\pull_zeppbridge.bat shorthand.
        if (rel == L"pull_zeppbridge.bat") rel = L"run\\pull_zeppbridge.bat";
        const std::wstring bat = RepoRoot() + L"\\scripts\\" + rel;
        // Pass confirm as first arg so bat can re-pass after UAC elevation.
        std::wstring params;
        if (!confirm.empty()) {
          params = L"\"";
          params += WidenUtf8(confirm);
          params += L"\"";
        }
        ShellExecuteW(nullptr, L"open", bat.c_str(),
                      params.empty() ? nullptr : params.c_str(), RepoRoot().c_str(), SW_SHOW);
      }
      return S_OK;
    }

    // Health page: turn CALT Sync receiver (:8765) on/off.
    // Native LLM HTTP (OpenJarvis/Ollama/OpenRouter) — WebView fetch hits CORS.
    // {"type":"llm_http","id":"...","method":"GET|POST","url":"...","authorization":"Bearer …",
    //  "content_type":"application/json","body":"...","timeout_ms":90000}
    if (msg.find("\"llm_http\"") != std::string::npos) {
      std::string id, method, url, authorization, contentType, body;
      int timeoutMs = 60000;
      bool base64Response = false;
      GrabJsonQuoted(msg, "id", &id);
      GrabJsonQuoted(msg, "method", &method);
      GrabJsonQuoted(msg, "url", &url);
      GrabJsonQuoted(msg, "authorization", &authorization);
      GrabJsonQuoted(msg, "content_type", &contentType);
      GrabJsonQuoted(msg, "body", &body);
      GrabJsonInt(msg, "timeout_ms", &timeoutMs);
      GrabJsonBool(msg, "base64_response", &base64Response);
      if (id.empty() || url.empty()) {
        const std::string err =
            "{\"type\":\"llm_http_result\",\"ok\":false,\"id\":\"" + JsonEscapeUtf8(id) +
            "\",\"error\":\"bad_request\"}";
        sender->PostWebMessageAsJson(WidenUtf8(err).c_str());
        return S_OK;
      }
      if (method.empty()) method = "GET";
      sender->AddRef();
      std::thread([sender, id, method, url, authorization, contentType, body, timeoutMs, base64Response]() {
        LlmHttpResult r =
            LlmHttpRequest(method, url, authorization, contentType, body, (unsigned)timeoutMs, base64Response);
        std::string out = std::string("{\"type\":\"llm_http_result\",\"id\":\"") +
                          JsonEscapeUtf8(id) + "\",\"ok\":" + (r.ok ? "true" : "false") +
                          ",\"status\":" + std::to_string(r.status) +
                          ",\"is_base64\":" + (r.is_base64 ? "true" : "false") +
                          ",\"body\":\"" + JsonEscapeUtf8(r.body) + "\"";
        if (!r.error.empty())
          out += std::string(",\"error\":\"") + JsonEscapeUtf8(r.error) + "\"";
        out += "}";
        PostEnforcerResultToUi(sender, WidenUtf8(out));
      }).detach();
      return S_OK;
    }

    // PC power (lock/sleep/shutdown) — interactive user session.
    // {"type":"pc_power","id":"...","op":"lock|sleep|shutdown|cancel","seconds":30,"v":1}
    if (msg.find("\"pc_power\"") != std::string::npos) {
      std::string id, op;
      int seconds = 30;
      GrabJsonQuoted(msg, "id", &id);
      GrabJsonQuoted(msg, "op", &op);
      GrabJsonInt(msg, "seconds", &seconds);
      PcPowerResult r;
      if (op == "lock")
        r = PcPowerRun(PcPowerOp::Lock);
      else if (op == "sleep")
        r = PcPowerRun(PcPowerOp::Sleep);
      else if (op == "shutdown")
        r = PcPowerShutdownIn(seconds > 0 ? (unsigned)seconds : 30);
      else if (op == "cancel")
        r = PcPowerRun(PcPowerOp::Cancel);
      else {
        r.ok = false;
        r.detail = "bad_op";
      }
      std::string body = std::string("{\"type\":\"pc_power_result\",\"id\":\"") +
                         JsonEscapeUtf8(id) + "\",\"op\":\"" + JsonEscapeUtf8(op) +
                         "\",\"ok\":" + (r.ok ? "true" : "false") + ",\"detail\":\"" +
                         JsonEscapeUtf8(r.detail) + "\"}";
      sender->PostWebMessageAsJson(WidenUtf8(body).c_str());
      return S_OK;
    }

    // Close browsers / named apps.
    // {"type":"pc_apps","id":"...","op":"browsers|named","names":"discord.exe,spotify.exe","v":1}
    if (msg.find("\"pc_apps\"") != std::string::npos) {
      std::string id, op, names;
      GrabJsonQuoted(msg, "id", &id);
      GrabJsonQuoted(msg, "op", &op);
      GrabJsonQuoted(msg, "names", &names);
      PcAppsResult r;
      if (op == "browsers" || op == "close_browsers") {
        r = PcAppsCloseBrowsers();
      } else if (op == "named" || op == "close_named") {
        std::vector<std::string> exes;
        std::string cur;
        for (char c : names) {
          if (c == ',' || c == ';' || c == ' ') {
            if (!cur.empty()) {
              exes.push_back(cur);
              cur.clear();
            }
          } else
            cur.push_back(c);
        }
        if (!cur.empty()) exes.push_back(cur);
        r = PcAppsCloseNamed(exes);
      } else {
        r.ok = false;
        r.detail = "bad_op";
      }
      std::string body = std::string("{\"type\":\"pc_apps_result\",\"id\":\"") +
                         JsonEscapeUtf8(id) + "\",\"op\":\"" + JsonEscapeUtf8(op) +
                         "\",\"ok\":" + (r.ok ? "true" : "false") + ",\"closed\":" +
                         std::to_string(r.closed) + ",\"detail\":\"" + JsonEscapeUtf8(r.detail) +
                         "\"}";
      sender->PostWebMessageAsJson(WidenUtf8(body).c_str());
      return S_OK;
    }

    // Telegram remote: status|save|start|stop|link_next
    // save fields: enabled, bot_token, allow_chat, allow_power, link_next, chat_ids:"1,2"
    if (msg.find("\"telegram_remote\"") != std::string::npos) {
      std::string id, op, botToken, chatIds;
      GrabJsonQuoted(msg, "id", &id);
      GrabJsonQuoted(msg, "op", &op);
      if (op.empty()) op = "status";

      bool actionOk = true;
      std::string actionErr;
      if (op == "save" || op == "start" || op == "stop" || op == "link_next") {
        TelegramRemoteConfig cfg = TelegramRemoteLoad();
        GrabJsonBool(msg, "enabled", &cfg.enabled);
        if (GrabJsonQuoted(msg, "bot_token", &botToken) && !botToken.empty())
          cfg.bot_token = botToken;
        GrabJsonBool(msg, "allow_chat", &cfg.allow_chat);
        GrabJsonBool(msg, "allow_power", &cfg.allow_power);
        GrabJsonBool(msg, "link_next", &cfg.link_next);
        if (GrabJsonQuoted(msg, "chat_ids", &chatIds)) {
          cfg.allowed_chat_ids.clear();
          size_t i = 0;
          while (i < chatIds.size()) {
            while (i < chatIds.size() &&
                   (chatIds[i] == ' ' || chatIds[i] == ',' || chatIds[i] == ';'))
              ++i;
            if (i >= chatIds.size()) break;
            bool neg = false;
            if (chatIds[i] == '-') {
              neg = true;
              ++i;
            }
            if (i >= chatIds.size() || chatIds[i] < '0' || chatIds[i] > '9') break;
            long long v = 0;
            while (i < chatIds.size() && chatIds[i] >= '0' && chatIds[i] <= '9') {
              v = v * 10 + (chatIds[i] - '0');
              ++i;
            }
            cfg.allowed_chat_ids.push_back(neg ? -v : v);
          }
        }
        if (op == "link_next") cfg.link_next = true;
        if (op == "stop") cfg.enabled = false;
        if (op == "start") cfg.enabled = true;
        if (!TelegramRemoteSave(cfg, &actionErr))
          actionOk = false;
        else
          TelegramRemoteApply(cfg);
      }

      std::string core = TelegramRemoteStatusJson();
      size_t brace = core.find('{');
      std::string inner =
          brace == std::string::npos ? "\"enabled\":false}" : core.substr(brace + 1);
      std::string body = std::string("{\"type\":\"telegram_remote_result\",\"ok\":") +
                         (actionOk ? "true" : "false") + ",\"id\":\"" + JsonEscapeUtf8(id) +
                         "\",\"op\":\"" + JsonEscapeUtf8(op) + "\"," + inner;
      if (!actionErr.empty() && !body.empty() && body.back() == '}') {
        body.pop_back();
        body += ",\"error\":\"" + JsonEscapeUtf8(actionErr) + "\"}";
      }
      sender->PostWebMessageAsJson(WidenUtf8(body).c_str());
      return S_OK;
    }

    // Background OpenJarvis API daemon (no desktop GUI).
    // {"type":"openjarvis_host","id":"...","op":"status|start|stop|ensure","model":"qwen3.5:2b","v":1}
    if (msg.find("\"openjarvis_host\"") != std::string::npos) {
      std::string id, op, model;
      GrabJsonQuoted(msg, "id", &id);
      GrabJsonQuoted(msg, "op", &op);
      GrabJsonQuoted(msg, "model", &model);
      if (op.empty()) op = "status";

      bool actionOk = true;
      std::string actionErr;
      if (op == "start" || op == "ensure")
        actionOk = OpenJarvisHostEnsure(model, &actionErr);
      else if (op == "stop")
        actionOk = OpenJarvisHostStop(&actionErr);

      OpenJarvisHostStatus st = OpenJarvisHostProbe();
      st.model = model;
      if (!actionErr.empty()) st.error = actionErr;
      std::string core = OpenJarvisHostStatusJson(st);
      size_t brace = core.find('{');
      std::string inner =
          brace == std::string::npos ? "\"reachable\":false}" : core.substr(brace + 1);
      std::string body = std::string("{\"type\":\"openjarvis_host_result\",\"ok\":") +
                         (actionOk ? "true" : "false") + ",\"id\":\"" + JsonEscapeUtf8(id) +
                         "\",\"op\":\"" + JsonEscapeUtf8(op) + "\"," + inner;
      const std::wstring w = WidenUtf8(body);
      sender->PostWebMessageAsJson(w.c_str());
      return S_OK;
    }

    // {"type":"sync_host","id":"...","op":"status|start|stop","v":1}
    // start/stop can take a few seconds — reply off the UI thread so FE does not hit timeout.
    if (msg.find("\"sync_host\"") != std::string::npos) {
      std::string id, op;
      GrabJsonQuoted(msg, "id", &id);
      GrabJsonQuoted(msg, "op", &op);
      if (op.empty()) op = "status";

      sender->AddRef();
      std::thread([sender, id, op]() {
        bool actionOk = true;
        std::string actionErr;
        if (op == "start")
          actionOk = SyncHostStart(&actionErr);
        else if (op == "stop")
          actionOk = SyncHostStop(&actionErr);

        SyncHostStatus st = SyncHostProbe();
        if (!actionErr.empty()) st.error = actionErr;
        std::string core = SyncHostStatusJson(st);
        size_t brace = core.find('{');
        std::string inner =
            brace == std::string::npos ? "\"running\":false}" : core.substr(brace + 1);
        std::string body = std::string("{\"type\":\"sync_host_result\",\"ok\":") +
                           (actionOk ? "true" : "false") + ",\"id\":\"" + JsonEscapeUtf8(id) +
                           "\",\"op\":\"" + JsonEscapeUtf8(op) + "\"," + inner;
        PostEnforcerResultToUi(sender, WidenUtf8(body));
      }).detach();
      return S_OK;
    }

    // Expect: {"type":"enforcer_cmd","id":"...","op":"...","payload":{...},"v":1}
    if (msg.find("\"enforcer_cmd\"") == std::string::npos) return S_OK;

    std::string id, op;
    auto grab = [&](const char* key, std::string* out) {
      std::string needle = std::string("\"") + key + "\"";
      size_t p = msg.find(needle);
      if (p == std::string::npos) return;
      size_t colon = msg.find(':', p + needle.size());
      if (colon == std::string::npos) return;
      size_t i = colon + 1;
      while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
      if (i >= msg.size() || msg[i] != '"') return;
      ++i;
      std::string val;
      while (i < msg.size() && msg[i] != '"') {
        if (msg[i] == '\\' && i + 1 < msg.size()) {
          val.push_back(msg[i + 1]);
          i += 2;
          continue;
        }
        val.push_back(msg[i++]);
      }
      *out = val;
    };
    grab("id", &id);
    grab("op", &op);

    std::string payload = "{}";
    {
      size_t p = msg.find("\"payload\"");
      if (p != std::string::npos) {
        size_t colon = msg.find(':', p + 9);
        if (colon != std::string::npos) {
          size_t i = colon + 1;
          while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
          if (i < msg.size() && msg[i] == '{') {
            int depth = 0;
            size_t start = i;
            for (; i < msg.size(); ++i) {
              if (msg[i] == '{')
                ++depth;
              else if (msg[i] == '}') {
                --depth;
                if (depth == 0) {
                  payload = msg.substr(start, i - start + 1);
                  break;
                }
              }
            }
          }
        }
      }
    }

    std::string req = std::string("{\"op\":\"") + op + "\",\"v\":1,\"id\":\"" + id +
                      "\",\"payload\":" + payload + "}";
    unsigned timeoutMs = 5000;
    if (op == "plan.overlay")
      timeoutMs = 16000;
    else if (op.rfind("plan.", 0) == 0)
      timeoutMs = 8000;
    else if (op.rfind("bible.devotion.", 0) == 0)
      timeoutMs = 15000;
    // Prefer FE-supplied timeoutMs when present (must be >= pipe budget).
    {
      std::string needle = "\"timeoutMs\"";
      size_t p = msg.find(needle);
      if (p != std::string::npos) {
        size_t colon = msg.find(':', p + needle.size());
        if (colon != std::string::npos) {
          size_t i = colon + 1;
          while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t')) ++i;
          unsigned parsed = 0;
          while (i < msg.size() && msg[i] >= '0' && msg[i] <= '9') {
            parsed = parsed * 10 + (unsigned)(msg[i] - '0');
            ++i;
          }
          if (parsed >= 800 && parsed <= 30000) timeoutMs = parsed;
        }
      }
    }
    // Off UI thread + single-flight pipe client (see EnforcerCmdQueue).
    EnforcerCmdQueue::Instance().Enqueue(sender, std::move(req), id, op, timeoutMs);
    return S_OK;
  }

 private:
  HWND shell_hwnd_ = nullptr;
  std::atomic<ULONG> ref_{1};
};

}  // namespace

WebViewHost::WebViewHost() = default;

WebViewHost::~WebViewHost() {
  if (webview_) {
    webview_->Release();
    webview_ = nullptr;
  }
  if (controller_) {
    controller_->Close();
    controller_->Release();
    controller_ = nullptr;
  }
  if (loader_) {
    FreeLibrary(loader_);
    loader_ = nullptr;
  }
}

bool WebViewHost::Init(HWND parent, const std::wstring& userDataDir, ReadyFn onReady) {
  parent_ = parent;
  if (parent) gEnforcerUiHwnd = parent;
  user_data_ = userDataDir;
  on_ready_ = std::move(onReady);

  loader_ = LoadWebView2Loader();
  if (!loader_) {
    if (on_ready_) {
      on_ready_(false);
    }
    return false;
  }

  auto createEnv = reinterpret_cast<CreateEnvFn>(
      GetProcAddress(loader_, "CreateCoreWebView2EnvironmentWithOptions"));
  if (!createEnv) {
    if (on_ready_) {
      on_ready_(false);
    }
    return false;
  }

  auto* handler = new EnvHandler([this](HRESULT hr, ICoreWebView2Environment* env) {
    OnEnvironment(hr, env);
  });
  const HRESULT hr = createEnv(nullptr, user_data_.c_str(), nullptr, handler);
  handler->Release();
  if (FAILED(hr)) {
    if (on_ready_) {
      on_ready_(false);
    }
    return false;
  }
  return true;
}

void WebViewHost::OnEnvironment(HRESULT hr, void* envVoid) {
  auto* env = static_cast<ICoreWebView2Environment*>(envVoid);
  if (FAILED(hr) || !env) {
    if (on_ready_) {
      on_ready_(false);
    }
    return;
  }
  auto* handler = new CtrlHandler([this](HRESULT ehr, ICoreWebView2Controller* ctrl) {
    OnController(ehr, ctrl);
  });
  const HRESULT chr = env->CreateCoreWebView2Controller(parent_, handler);
  handler->Release();
  if (FAILED(chr)) {
    if (on_ready_) {
      on_ready_(false);
    }
  }
}

void WebViewHost::OnController(HRESULT hr, ICoreWebView2Controller* controller) {
  if (FAILED(hr) || !controller) {
    if (on_ready_) {
      on_ready_(false);
    }
    return;
  }
  controller_ = controller;
  controller_->AddRef();

  ICoreWebView2* wv = nullptr;
  if (FAILED(controller_->get_CoreWebView2(&wv)) || !wv) {
    if (on_ready_) {
      on_ready_(false);
    }
    return;
  }
  webview_ = wv;
  Resize();
  controller_->put_IsVisible(TRUE);
  {
    EventRegistrationToken token{};
    auto* msg = new MsgHandler(parent_);
    webview_->add_WebMessageReceived(msg, &token);
    msg->Release();
  }
  {
    EventRegistrationToken token{};
    auto* nav = new NavStartingHandler();
    webview_->add_NavigationStarting(nav, &token);
    nav->Release();
  }
  {
    EventRegistrationToken token{};
    auto* nw = new NewWindowHandler();
    webview_->add_NewWindowRequested(nw, &token);
    nw->Release();
  }
  // Also block iframe navigations to the open web (e.g. Math→ML embed tricks).
  {
    EventRegistrationToken token{};
    auto* frameNav = new NavStartingHandler();
    webview_->add_FrameNavigationStarting(frameNav, &token);
    frameNav->Release();
  }
  ready_ = true;
  if (on_ready_) {
    on_ready_(true);
  }
}

bool WebViewHost::MapStaticSite(const std::wstring& folderAbsolute) {
  if (!webview_ || folderAbsolute.empty()) {
    return false;
  }
  ICoreWebView2_3* wv3 = nullptr;
  if (FAILED(webview_->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(&wv3))) ||
      !wv3) {
    return false;
  }
  const HRESULT hr = wv3->SetVirtualHostNameToFolderMapping(
      L"calt.app",
      folderAbsolute.c_str(),
      COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
  wv3->Release();
  static_mapped_ = SUCCEEDED(hr);
  return static_mapped_;
}

bool WebViewHost::MapBehaviorData(const std::wstring& folderAbsolute) {
  if (!webview_ || folderAbsolute.empty()) {
    return false;
  }
  ICoreWebView2_3* wv3 = nullptr;
  if (FAILED(webview_->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(&wv3))) ||
      !wv3) {
    return false;
  }
  const HRESULT hr = wv3->SetVirtualHostNameToFolderMapping(
      L"calt-data.app",
      folderAbsolute.c_str(),
      COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
  wv3->Release();
  behavior_mapped_ = SUCCEEDED(hr);
  return behavior_mapped_;
}

bool WebViewHost::MapBibleData(const std::wstring& folderAbsolute) {
  if (!webview_ || folderAbsolute.empty()) {
    return false;
  }
  ICoreWebView2_3* wv3 = nullptr;
  if (FAILED(webview_->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(&wv3))) ||
      !wv3) {
    return false;
  }
  const HRESULT hr = wv3->SetVirtualHostNameToFolderMapping(
      L"calt-bible.app",
      folderAbsolute.c_str(),
      COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
  wv3->Release();
  return SUCCEEDED(hr);
}

bool WebViewHost::MapLearnRoadmap(const std::wstring& folderAbsolute) {
  if (!webview_ || folderAbsolute.empty()) {
    return false;
  }
  ICoreWebView2_3* wv3 = nullptr;
  if (FAILED(webview_->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(&wv3))) ||
      !wv3) {
    return false;
  }
  const HRESULT hr = wv3->SetVirtualHostNameToFolderMapping(
      L"calt-learn.app",
      folderAbsolute.c_str(),
      COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
  wv3->Release();
  return SUCCEEDED(hr);
}

void WebViewHost::Resize() {
  if (!controller_ || !parent_) {
    return;
  }
  RECT rc{};
  GetClientRect(parent_, &rc);
  controller_->put_Bounds(rc);
}

void WebViewHost::Navigate(const std::wstring& url) {
  if (!webview_) {
    return;
  }
  webview_->Navigate(url.c_str());
}

bool WebViewHost::PostWebMessageJson(const std::string& utf8Json) {
  if (!webview_ || utf8Json.empty()) {
    return false;
  }
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8Json.c_str(), (int)utf8Json.size(), nullptr, 0);
  if (n <= 0) return false;
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8Json.c_str(), (int)utf8Json.size(), w.data(), n);
  return SUCCEEDED(webview_->PostWebMessageAsJson(w.c_str()));
}

void WebViewHost::NavigateHash(const std::wstring& hashWithQuery) {
  if (!webview_) {
    return;
  }
  // Escape for JS string literal.
  std::wstring esc;
  esc.reserve(hashWithQuery.size() + 8);
  for (wchar_t c : hashWithQuery) {
    if (c == L'\\' || c == L'\'' || c == L'"') {
      esc.push_back(L'\\');
    }
    if (c == L'\n' || c == L'\r') {
      continue;
    }
    esc.push_back(c);
  }
  std::wstring script =
      L"(function(){try{var h='" + esc +
      L"';if(h.charAt(0)!=='#')h='#'+h;"
      L"if(location.hash!==h){location.hash=h;}else{"
      L"window.dispatchEvent(new HashChangeEvent('hashchange'));}"
      L"}catch(e){}})();";
  webview_->ExecuteScript(script.c_str(), nullptr);
}

void WebViewHost::Show(bool show) {
  if (!controller_) {
    return;
  }
  controller_->put_IsVisible(show ? TRUE : FALSE);
}

void WebViewHost::SetEnforcerUiHwnd(HWND hwnd) {
  if (hwnd) gEnforcerUiHwnd = hwnd;
}

bool WebViewHost::DeliverEnforcerCmdResult(LPARAM lParam) {
  auto* msg = reinterpret_cast<EnforcerCmdUiMsg*>(lParam);
  if (!msg) return false;
  if (msg->sender) {
    msg->sender->PostWebMessageAsJson(msg->json.c_str());
    msg->sender->Release();
  }
  delete msg;
  return true;
}
