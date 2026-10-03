#include "tray_icon.h"

#include <shellapi.h>

#include <cstring>

namespace {

void CopyW(wchar_t* dst, size_t dstChars, const wchar_t* src) {
  if (!dst || dstChars == 0) {
    return;
  }
  if (!src) {
    dst[0] = 0;
    return;
  }
  wcsncpy(dst, src, dstChars - 1);
  dst[dstChars - 1] = 0;
}

}  // namespace

TrayIcon::TrayIcon() {
  ZeroMemory(&nid_, sizeof(nid_));
}

TrayIcon::~TrayIcon() {
  Destroy();
}

bool TrayIcon::Create(HWND messageHwnd, HICON icon, const wchar_t* tip) {
  hwnd_ = messageHwnd;
  icon_ = icon;
  CopyW(tip_, ARRAYSIZE(tip_), tip ? tip : L"CALT Focus");
  return EnsureAdded();
}

bool TrayIcon::EnsureAdded() {
  if (!hwnd_) {
    return false;
  }
  if (added_) {
    return true;
  }
  ZeroMemory(&nid_, sizeof(nid_));
  nid_.cbSize = sizeof(NOTIFYICONDATAW);
  nid_.hWnd = hwnd_;
  nid_.uID = 1;
  // NIF_SHOWTIP keeps the tip usable under NOTIFYICON_VERSION_4 (Win10/11).
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid_.uCallbackMessage = WM_TRAY;
  nid_.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
  CopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), tip_);
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE) {
      added_ = true;
      nid_.uVersion = NOTIFYICON_VERSION_4;
      Shell_NotifyIconW(NIM_SETVERSION, &nid_);
      return true;
    }
    Sleep(150);
  }
  // Last resort: legacy add without VERSION_4.
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  if (Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE) {
    added_ = true;
    return true;
  }
  return false;
}

void TrayIcon::Destroy() {
  if (added_) {
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
  }
}

void TrayIcon::SetTooltip(const wchar_t* tip) {
  if (!tip) {
    return;
  }
  CopyW(tip_, ARRAYSIZE(tip_), tip);
  if (!added_) {
    return;
  }
  nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
  CopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), tip_);
  Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::ShowBalloon(const wchar_t* title, const wchar_t* body) {
  if (!EnsureAdded()) {
    return;
  }
  nid_.uFlags = NIF_INFO | NIF_SHOWTIP;
  CopyW(nid_.szInfoTitle, ARRAYSIZE(nid_.szInfoTitle), title ? title : L"CALT Focus");
  CopyW(nid_.szInfo, ARRAYSIZE(nid_.szInfo), body ? body : L"");
  nid_.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
  Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

bool TrayIcon::IsAdded() const {
  return added_;
}

void TrayIcon::PopupMenu() {
  if (!EnsureAdded()) {
    MessageBoxW(
        hwnd_,
        L"System tray icon could not be created.\n\n"
        L"On Windows 11: click the ^ chevron near the clock and look for "
        L"\"CALT Focus\".\n"
        L"Or use the Focus menu (•••) in the top bar.",
        L"CALT Focus",
        MB_OK | MB_ICONINFORMATION);
    return;
  }
  POINT pt{};
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();
  HMENU adv = CreatePopupMenu();

  AppendMenuW(menu, MF_STRING, ID_OPEN, L"Open Focus");
  AppendMenuW(menu, MF_STRING, ID_CALENDAR, L"Open Calendar");
  AppendMenuW(menu, MF_STRING, ID_PLAN, L"Open Plan");
  AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Open Settings");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ID_RUN, L"Ensure enforcer + open");
  AppendMenuW(menu, MF_STRING, ID_RESTART_ENFORCER, L"Restart enforcer (tracker)");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ID_RELOAD_UI, L"Reload UI");
  AppendMenuW(menu, MF_STRING, ID_UPDATE_UI, L"Update UI (rebuild + reload)");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  AppendMenuW(adv, MF_STRING, ID_API, L"Start Study API (:8000)");
  AppendMenuW(adv, MF_STRING, ID_FE, L"Start Study Frontend (Vite :5173)");
  AppendMenuW(adv, MF_STRING, ID_STACK, L"Start Study API + Vite");
  AppendMenuW(adv, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(adv, MF_STRING, ID_UPDATE_STACK, L"Update stack (UI + natives + rules)");
  AppendMenuW(adv, MF_STRING, ID_APPLY_UPDATE, L"Apply pending update && restart");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(adv), L"Advanced (Study / rebuild)");

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ID_QUIT, L"Quit");
  SetMenuDefaultItem(menu, ID_OPEN, FALSE);
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_,
                 nullptr);
  DestroyMenu(menu);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
}

LRESULT TrayIcon::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_TRAY) {
    // NOTIFYICON_VERSION_4: LOWORD(lParam) is the event (NIN_* / WM_*).
    switch (LOWORD(lParam)) {
      case WM_LBUTTONDBLCLK:
      case NIN_SELECT:
      case NIN_KEYSELECT:
        if (on_open_) {
          on_open_();
        } else if (on_run_) {
          on_run_();
        }
        return 0;
      case WM_RBUTTONUP:
      case WM_CONTEXTMENU:
        PopupMenu();
        return 0;
      case WM_LBUTTONUP:
        if (on_open_) {
          on_open_();
        } else if (on_run_) {
          on_run_();
        }
        return 0;
      default:
        break;
    }
  }
  if (msg == WM_COMMAND) {
    switch (LOWORD(wParam)) {
      case ID_RUN:
        if (on_run_) on_run_();
        return 0;
      case ID_STACK:
        if (on_stack_) on_stack_();
        return 0;
      case ID_API:
        if (on_api_) on_api_();
        return 0;
      case ID_FE:
        if (on_fe_) on_fe_();
        return 0;
      case ID_OPEN:
        if (on_open_) on_open_();
        return 0;
      case ID_SETTINGS:
        if (on_settings_) on_settings_();
        return 0;
      case ID_CALENDAR:
        if (on_calendar_) on_calendar_();
        return 0;
      case ID_PLAN:
        if (on_plan_) on_plan_();
        return 0;
      case ID_PROD:
        if (on_prod_) on_prod_();
        return 0;
      case ID_RELOAD_UI:
        if (on_reload_ui_) on_reload_ui_();
        return 0;
      case ID_UPDATE_UI:
        if (on_update_ui_) on_update_ui_();
        return 0;
      case ID_UPDATE_STACK:
        if (on_update_stack_) on_update_stack_();
        return 0;
      case ID_APPLY_UPDATE:
        if (on_apply_update_) on_apply_update_();
        return 0;
      case ID_RESTART_ENFORCER:
        if (on_restart_enforcer_) on_restart_enforcer_();
        return 0;
      case ID_QUIT:
        if (on_quit_) on_quit_();
        return 0;
      default:
        break;
    }
  }
  return 0;
}
