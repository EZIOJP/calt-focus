#include "webview_host.h"
#include "enforcer_cmd.h"
#include "paths.h"

#include <WebView2.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <string>

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

class ScriptHandler : public ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler {
 public:
  explicit ScriptHandler(std::function<void(HRESULT)> cb) : cb_(std::move(cb)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler) {
      *ppv = static_cast<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*>(this);
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
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, LPCWSTR) override {
    cb_(errorCode);
    return S_OK;
  }

 private:
  std::function<void(HRESULT)> cb_;
  std::atomic<ULONG> ref_{1};
};

// Home card still says Jarvis in the Study bundle. This renames that chrome to Qwen
// and plays speechSynthesis through Piper on :8765, falling back to the Edge voice.
const wchar_t kQwenSpeechJs[] =
    LR"QWEN((function(){
if(window.__caltQwenSpeech)return;
window.__caltQwenSpeech=1;
var audio=null, gen=0, tail=Promise.resolve();
function rename(root){
  if(!root)return;
  var w=document.createTreeWalker(root,NodeFilter.SHOW_TEXT);
  var n;
  while((n=w.nextNode())){
    var p=n.parentNode, tag=p&&p.tagName;
    if(!n.nodeValue||n.nodeValue.indexOf("Jarvis")<0)continue;
    if(tag==="SCRIPT"||tag==="STYLE"||tag==="TEXTAREA")continue;
    if(n.nodeValue.length>800)continue;
    n.nodeValue=n.nodeValue.replace(/Jarvis/g,"Qwen");
  }
}
var scheduled=false;
function schedule(){
  if(scheduled||!document.body)return;
  scheduled=true;
  requestAnimationFrame(function(){scheduled=false;rename(document.body);});
}
function retitle(){
  var inputs=document.querySelectorAll("input[type=text]");
  for(var i=0;i<inputs.length;i++){
    var ph=inputs[i].getAttribute("placeholder")||"";
    if(ph.indexOf("Command or hold mic")<0 && ph.indexOf("Talk to Qwen")<0) continue;
    inputs[i].setAttribute("placeholder","Talk to Qwen - add a task, plan an hour, brief me");
  }
}
function arm(){
  schedule();
  retitle();
  new MutationObserver(function(){schedule();retitle();}).observe(document.body,{subtree:true,childList:true,characterData:true});
}
if(document.body)arm(); else document.addEventListener("DOMContentLoaded",arm);
function showSay(root, say){
  if(!root) return;
  var paras=root.querySelectorAll("p");
  var line=null;
  for(var i=0;i<paras.length;i++){
    var t=(paras[i].textContent||"").trim();
    if(t==="Qwen"||t==="Jarvis") continue;
    line=paras[i];
    break;
  }
  if(line) line.textContent=say;
}
function qwenTalk(message, root){
  fetch("http://127.0.0.1:8765/api/assistant/talk",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({message:message})})
    .then(function(res){return res.json();})
    .then(function(data){
      var say=(data&&data.say)||(data&&data.detail)||"Qwen did not answer.";
      var extra=data&&data.actions&&data.actions[0]&&data.actions[0].detail;
      if(extra && say.indexOf(extra)<0) say=say+" "+extra;
      showSay(root, say);
      if(window.speechSynthesis) window.speechSynthesis.speak({text:say.slice(0,480)});
    }).catch(function(){});
}
function weapon(line){
  var s=String(line||"").toLowerCase().replace(/^\//,"").trim();
  return /^(softland|sl|pass|daypass|speak|voice|stop|report|whatsapp|wa)\b/.test(s);
}
document.addEventListener("submit",function(ev){
  var form=ev.target;
  if(!form||!form.querySelector) return;
  var input=form.querySelector("input[placeholder*='Talk to Qwen'], input[placeholder*='Command or hold mic']");
  if(!input) return;
  var message=(input.value||"").trim();
  if(!message||weapon(message)) return;
  ev.preventDefault();
  ev.stopPropagation();
  var proto=Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype,"value");
  if(proto&&proto.set) proto.set.call(input,"");
  else input.value="";
  input.dispatchEvent(new Event("input",{bubbles:true}));
  qwenTalk(message, form.closest("section")||form.parentElement);
},true);
document.addEventListener("click",function(ev){
  var b=ev.target&&ev.target.closest?ev.target.closest("button"):null;
  if(!b) return;
  if((b.textContent||"").replace(/\s+/g," ").trim()!=="Brief") return;
  ev.preventDefault();
  ev.stopPropagation();
  qwenTalk("Brief my day. Name the next task and whether the plan is set.", b.closest("section")||b.parentElement);
},true);
var synth=window.speechSynthesis;
if(!synth||synth.__caltPiped)return;
var nativeSpeak=synth.speak.bind(synth);
var nativeCancel=synth.cancel.bind(synth);
synth.cancel=function(){gen++; if(audio){try{audio.pause();}catch(e){} audio=null;} tail=Promise.resolve(); try{nativeCancel();}catch(e){}};
synth.speak=function(utter){
  var text=utter&&utter.text?String(utter.text):"";
  if(!text)return nativeSpeak(utter);
  var voice="qwen";
  try{
    var prefs=JSON.parse(localStorage.getItem("calt:focus-jarvis:v1")||"{}");
    if(prefs.voiceMode==="normal")voice="normal";
    if(prefs.speak===false)return;
  }catch(e){}
  var ticket=gen;
  tail=tail.then(function(){
    if(ticket!==gen)return;
    return fetch("http://127.0.0.1:8765/api/speech",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({text:text.slice(0,480),voice:voice})})
      .then(function(res){if(!res.ok)throw new Error("tts");return res.blob();})
      .then(function(blob){
        if(ticket!==gen)return;
        var url=URL.createObjectURL(blob);
        audio=new Audio(url);
        audio.onended=function(){URL.revokeObjectURL(url);};
        return audio.play();
      }).catch(function(){ if(ticket===gen) nativeSpeak(utter); });
  }).catch(function(){});
};
synth.__caltPiped=1;
})();)QWEN";

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

class MsgHandler : public ICoreWebView2WebMessageReceivedEventHandler {
 public:
  explicit MsgHandler(HWND shellHwnd) : shell_hwnd_(shellHwnd) {}
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
    std::string resp;
    std::string out;
    if (!EnforcerSendCommand(req, resp)) {
      out = std::string("{\"type\":\"enforcer_cmd_result\",\"ok\":false,\"id\":\"") + id +
            "\",\"error\":\"enforcer_unreachable\"}";
    } else if (!resp.empty() && resp[0] == '{') {
      out = std::string("{\"type\":\"enforcer_cmd_result\",") + resp.substr(1);
    } else {
      out = std::string("{\"type\":\"enforcer_cmd_result\",\"ok\":false,\"id\":\"") + id +
            "\",\"error\":\"bad_response\"}";
    }
    std::wstring w = WidenUtf8(out);
    sender->PostWebMessageAsJson(w.c_str());
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
  ReadyFn finish = on_ready_;
  auto* script = new ScriptHandler([finish](HRESULT) {
    if (finish) finish(true);
  });
  if (FAILED(webview_->AddScriptToExecuteOnDocumentCreated(kQwenSpeechJs, script))) {
    script->Release();
    if (finish) finish(true);
    return;
  }
  script->Release();
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
