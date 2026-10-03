#pragma once

#include <windows.h>
#include <shellapi.h>

#include <functional>

// Win32 NotifyIcon tray for calt_focus.

class TrayIcon {
 public:
  using Callback = std::function<void()>;

  TrayIcon();
  ~TrayIcon();

  bool Create(HWND messageHwnd, HICON icon, const wchar_t* tip);
  /** Re-add after explorer restart / failed first add. */
  bool EnsureAdded();
  void Destroy();
  void SetTooltip(const wchar_t* tip);
  void ShowBalloon(const wchar_t* title, const wchar_t* body);
  bool IsAdded() const;
  void PopupMenu();

  void SetOnRun(Callback cb) { on_run_ = std::move(cb); }
  void SetOnStartStack(Callback cb) { on_stack_ = std::move(cb); }
  void SetOnStartApi(Callback cb) { on_api_ = std::move(cb); }
  void SetOnStartFe(Callback cb) { on_fe_ = std::move(cb); }
  void SetOnOpen(Callback cb) { on_open_ = std::move(cb); }
  void SetOnSettings(Callback cb) { on_settings_ = std::move(cb); }
  void SetOnCalendar(Callback cb) { on_calendar_ = std::move(cb); }
  void SetOnPlan(Callback cb) { on_plan_ = std::move(cb); }
  void SetOnProductivity(Callback cb) { on_prod_ = std::move(cb); }
  void SetOnReloadUi(Callback cb) { on_reload_ui_ = std::move(cb); }
  void SetOnUpdateUi(Callback cb) { on_update_ui_ = std::move(cb); }
  void SetOnUpdateStack(Callback cb) { on_update_stack_ = std::move(cb); }
  void SetOnApplyUpdate(Callback cb) { on_apply_update_ = std::move(cb); }
  void SetOnRestartEnforcer(Callback cb) { on_restart_enforcer_ = std::move(cb); }
  void SetOnQuit(Callback cb) { on_quit_ = std::move(cb); }

  LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  static constexpr UINT WM_TRAY = WM_APP + 41;
  static constexpr UINT ID_RUN = 4000;
  static constexpr UINT ID_STACK = 4004;
  static constexpr UINT ID_API = 4005;
  static constexpr UINT ID_FE = 4006;
  static constexpr UINT ID_OPEN = 4001;
  static constexpr UINT ID_SETTINGS = 4007;
  static constexpr UINT ID_CALENDAR = 4008;
  static constexpr UINT ID_PLAN = 4009;
  static constexpr UINT ID_PROD = 4002;
  static constexpr UINT ID_RELOAD_UI = 4010;
  static constexpr UINT ID_UPDATE_UI = 4011;
  static constexpr UINT ID_UPDATE_STACK = 4012;
  static constexpr UINT ID_APPLY_UPDATE = 4013;
  static constexpr UINT ID_RESTART_ENFORCER = 4014;
  static constexpr UINT ID_QUIT = 4003;

 private:
  NOTIFYICONDATAW nid_{};
  HWND hwnd_ = nullptr;
  HICON icon_ = nullptr;
  wchar_t tip_[128]{};
  bool added_ = false;
  Callback on_run_;
  Callback on_stack_;
  Callback on_api_;
  Callback on_fe_;
  Callback on_open_;
  Callback on_settings_;
  Callback on_calendar_;
  Callback on_plan_;
  Callback on_prod_;
  Callback on_reload_ui_;
  Callback on_update_ui_;
  Callback on_update_stack_;
  Callback on_apply_update_;
  Callback on_restart_enforcer_;
  Callback on_quit_;
};
