#pragma once

#include <windows.h>

#include <functional>
#include <string>

struct ICoreWebView2Controller;
struct ICoreWebView2;

// Hosts Edge WebView2 inside a child HWND. Does not kill processes.

class WebViewHost {
 public:
  using ReadyFn = std::function<void(bool ok)>;

  WebViewHost();
  ~WebViewHost();

  bool Init(HWND parent, const std::wstring& userDataDir, ReadyFn onReady);
  // Map https://calt.app → Vite dist/ folder (precompiled UI, no Vite process).
  bool MapStaticSite(const std::wstring& folderAbsolute);
  // Map https://calt-data.app → data/behavior (softland_policy.json, enforcer_status.json).
  bool MapBehaviorData(const std::wstring& folderAbsolute);
  // Map https://calt-bible.app → data/bible (WEB corpus for Focus reader).
  bool MapBibleData(const std::wstring& folderAbsolute);
  void Resize();
  void Navigate(const std::wstring& url);
  /** Change only the hash (SPA) — no full document reload. */
  void NavigateHash(const std::wstring& hashWithQuery);
  /** Push JSON to the page (Phase C status_tick). UTF-8 JSON string. */
  bool PostWebMessageJson(const std::string& utf8Json);
  void Show(bool show);
  bool Ready() const { return ready_; }
  bool StaticMapped() const { return static_mapped_; }
  bool BehaviorMapped() const { return behavior_mapped_; }

  HWND Parent() const { return parent_; }

 private:
  void OnEnvironment(HRESULT hr, void* env);
  void OnController(HRESULT hr, ICoreWebView2Controller* controller);

  HWND parent_ = nullptr;
  std::wstring user_data_;
  ReadyFn on_ready_;
  bool ready_ = false;
  bool static_mapped_ = false;
  bool behavior_mapped_ = false;
  ICoreWebView2Controller* controller_ = nullptr;
  ICoreWebView2* webview_ = nullptr;
  HMODULE loader_ = nullptr;
};
