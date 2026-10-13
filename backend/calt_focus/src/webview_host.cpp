#include "webview_host.h"
#include "enforcer_cmd.h"
#include "paths.h"

#include <WebView2.h>
#include <shellapi.h>
#include <shlwapi.h>

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
      // Calendar overlay jumps the queue so status probes cannot starve it.
      if (job.op == "plan.overlay") {
        jobs_.push_front(std::move(job));
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
      if (wp) PostMessageW(shell_hwnd_, WM_FOCUS_SHELL, wp, 0);
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
        ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
      if (name == "device_block_apply.bat" || name == "device_block_remove.bat") {
        const std::wstring bat = RepoRoot() + L"\\scripts\\" + WidenUtf8(name);
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
