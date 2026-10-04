#pragma once

#include "tray_icon.h"
#include "webview_host.h"

#include <memory>
#include <string>

class FocusApp {
 public:
  int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void CreateMainWindow(HINSTANCE instance);
  void CreateMessageWindow(HINSTANCE instance);
  void ShowFocusWindow(bool maximize = false);
  void ShowOfflinePage();
  void RunStack();
  void StartWebStack();
  void StartApiOnly();
  void StartFrontendOnly();
  void EnsureEnforcer();
  /** Stop + start CALTEnforcer (or console fallback). Desktop tracker rides along. */
  void RestartEnforcer();
  bool EnsurePrebuiltUiReady();
  bool EnsureFocusStaticServer();
  bool RunLifecycleCommand(const wchar_t* command, bool wait);
  bool WaitForPort(unsigned short port, DWORD timeoutMs);
  bool PortListening(unsigned short port);
  bool BibleDoneTodayFromMirror() const;
  void WakeOpenBibleGate();
  void OpenProductivity();
  void OpenSettings();
  void OpenCalendar();
  void OpenPlan();
  void OpenBible();
  void NavigateShell(const std::wstring& url);
  void ReloadUi();
  void UpdateUi();
  void UpdateStack();
  void ApplyPendingUpdate();
  bool RunNpmBuildFocus();
  bool RunUpdateStackScript();
  bool PendingUpdateNeedsRestart() const;
  std::wstring SettingsUrlBusted() const;
  /** Full document URL with ?_ui= bust before # (forces module reload). */
  std::wstring DocumentUrlBusted(const std::wstring& urlWithHash) const;
  void Quit();
  void RefreshTrayTip();
  /** Phase C: push mirror snapshot into WebView (no pipe). */
  void PushStatusTick();
  HICON MakeDotIcon();

  HINSTANCE instance_ = nullptr;
  HWND main_hwnd_ = nullptr;
  HWND msg_hwnd_ = nullptr;
  std::unique_ptr<TrayIcon> tray_;
  std::unique_ptr<WebViewHost> webview_;
  std::wstring pending_url_;
  bool webview_failed_ = false;
  UINT_PTR tip_timer_ = 0;
  UINT_PTR push_timer_ = 0;
  std::string last_status_push_fp_;
  std::wstring last_kill_toast_;
  bool kill_toast_primed_ = false;
};
