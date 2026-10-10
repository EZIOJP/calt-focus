#include <winsock2.h>
#include <ws2tcpip.h>

#include "app.h"

#include "paths.h"
#include "status_badge.h"
#include "wearable_listen.h"

#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr wchar_t kMainClass[] = L"CALTFocusMain";
constexpr wchar_t kMsgClass[] = L"CALTFocusMsg";
constexpr UINT kTipTimerId = 77;
constexpr UINT kPushTimerId = 78;
constexpr UINT WM_FOCUS_SHELL = WM_APP + 55;
constexpr WPARAM SHELL_RELOAD_UI = 1;
constexpr WPARAM SHELL_ENSURE_TRAY = 2;
constexpr WPARAM SHELL_QUIT = 3;
constexpr WPARAM SHELL_OPEN_SETTINGS = 4;
constexpr WPARAM SHELL_POPUP_TRAY = 5;
constexpr WPARAM SHELL_RESTART_ENFORCER = 6;
constexpr WPARAM SHELL_WAKE_BIBLE = 7;

UINT g_taskbar_created = 0;

bool AlreadyRunning() {
  HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\CALT.FocusShell.A");
  if (!mutex) {
    return false;
  }
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

void ActivateExisting() {
  HWND hwnd = FindWindowW(kMainClass, nullptr);
  if (hwnd) {
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(hwnd);
    // Wake existing shell into the morning Bible gate when needed.
    PostMessageW(hwnd, WM_FOCUS_SHELL, SHELL_WAKE_BIBLE, 0);
  }
}

std::wstring PythonExe() {
  const std::wstring venv = RepoRoot() + L"\\.venv\\Scripts\\python.exe";
  if (GetFileAttributesW(venv.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return venv;
  }
  return L"python";
}

}  // namespace

int FocusApp::Run(HINSTANCE instance) {
  instance_ = instance;
  if (AlreadyRunning()) {
    ActivateExisting();
    return 0;
  }

  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  CreateMessageWindow(instance);
  CreateMainWindow(instance);

  if (!g_taskbar_created) {
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  }

  // Tray must use a real top-level HWND — HWND_MESSAGE often fails on Win11.
  tray_ = std::make_unique<TrayIcon>();
  HICON icon = MakeDotIcon();
  if (!tray_->Create(main_hwnd_, icon, L"CALT Focus")) {
    MessageBoxW(
        main_hwnd_,
        L"Could not add the system tray icon.\n\n"
        L"Windows 11 often hides new icons — click the ^ chevron near the clock "
        L"and pin \"CALT Focus\".\n\n"
        L"Use the Focus menu (•••) in the top bar for Reload / Settings / Quit.",
        L"CALT Focus",
        MB_OK | MB_ICONWARNING);
  }
  tray_->SetOnRun([this]() { RunStack(); });
  tray_->SetOnStartStack([this]() { StartWebStack(); });
  tray_->SetOnStartApi([this]() { StartApiOnly(); });
  tray_->SetOnStartFe([this]() { StartFrontendOnly(); });
  tray_->SetOnOpen([this]() { WakeOpenBibleGate(); });
  tray_->SetOnSettings([this]() { OpenSettings(); });
  tray_->SetOnCalendar([this]() { OpenCalendar(); });
  tray_->SetOnPlan([this]() { OpenPlan(); });
  tray_->SetOnProductivity([this]() { OpenCalendar(); });
  tray_->SetOnReloadUi([this]() { ReloadUi(); });
  tray_->SetOnUpdateUi([this]() { UpdateUi(); });
  tray_->SetOnUpdateStack([this]() { UpdateStack(); });
  tray_->SetOnApplyUpdate([this]() { ApplyPendingUpdate(); });
  tray_->SetOnRestartEnforcer([this]() { RestartEnforcer(); });
  tray_->SetOnQuit([this]() { Quit(); });

  // Phone CALT Sync POSTs here. The enforcer stores the dump; this process only accepts HTTP.
  EnsureEnforcer();
  const bool watchPort = WearableListenStart(DataBehaviorDir());
  if (tray_ && tray_->IsAdded()) {
    if (!watchPort) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"The watch ingest port is already in use, so dumps are not reaching the enforcer. Quit the Python wearables hub and open Focus again.");
    } else {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"Running in the system tray. Win11: click ^ near the clock if the icon is hidden.");
    }
  }

  webview_ = std::make_unique<WebViewHost>();
  pending_url_ = CalendarUrl();
  webview_->Init(main_hwnd_, WebViewUserDataDir(), [this](bool ok) {
    webview_failed_ = !ok;
    if (ok) {
      ShowWindow(main_hwnd_, SW_SHOW);
      SetForegroundWindow(main_hwnd_);
      if (HasPrebuiltWebUi()) {
        if (EnsurePrebuiltUiReady()) {
          pending_url_ = CalendarUrl();
          webview_->Navigate(pending_url_);
          // Phase 7: do NOT auto-start Study :8000 — SoftLand/Arm work offline.
          // Manual tray "Start API" still calls StartApiOnly().
          if (tray_) {
            tray_->ShowBalloon(
                L"CALT Focus",
                L"Productivity ready — SoftLand/Arm/Plan via enforcer (no Study API).");
          }
          WakeOpenBibleGate();
        } else {
          ShowOfflinePage();
        }
      } else if (PortListening(5173)) {
        webview_->Navigate(pending_url_);
      } else {
        ShowOfflinePage();
        if (tray_) {
          tray_->ShowBalloon(
              L"CALT Focus",
              L"No prebuilt UI — run npm run build:focus once");
        }
      }
    } else {
      ShellExecuteW(nullptr, L"open", pending_url_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      MessageBoxW(
          nullptr,
          L"WebView2 failed to start.\n\n"
          L"Install the Evergreen WebView2 Runtime.\n"
          L"Tray → Open Focus after fixing WebView2.",
          L"CALT Focus",
          MB_OK | MB_ICONWARNING);
    }
  });

  tip_timer_ = SetTimer(msg_hwnd_, kTipTimerId, 2500, nullptr);
  // Push mirrors into WebView; content-hash skip avoids re-render spam.
  // 8s feels live while enforcer DB/plan sync stays at 1 min.
  push_timer_ = SetTimer(msg_hwnd_, kPushTimerId, 8000, nullptr);
  RefreshTrayTip();
  PushStatusTick();

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (tip_timer_) {
    KillTimer(msg_hwnd_, tip_timer_);
  }
  if (push_timer_) {
    KillTimer(msg_hwnd_, push_timer_);
  }
  tray_.reset();
  webview_.reset();
  CoUninitialize();
  WSACleanup();
  return 0;
}

void FocusApp::CreateMainWindow(HINSTANCE instance) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = FocusApp::WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kMainClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);

  main_hwnd_ = CreateWindowExW(
      0, kMainClass, L"CALT Focus — Productivity", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1100, 740,
      nullptr, nullptr, instance, this);

  // Match Focus dark chrome (#050505) — kills the default light Windows caption.
  if (main_hwnd_) {
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
    BOOL dark = TRUE;
    DwmSetWindowAttribute(main_hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x05, 0x05, 0x05);
    const COLORREF border = RGB(0x05, 0x05, 0x05);
    const COLORREF text = RGB(0xe8, 0xea, 0xed);
    DwmSetWindowAttribute(main_hwnd_, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(main_hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    DwmSetWindowAttribute(main_hwnd_, DWMWA_TEXT_COLOR, &text, sizeof(text));
    // Session unlock → bring Focus up full-screen for the morning Bible gate.
    WTSRegisterSessionNotification(main_hwnd_, NOTIFY_FOR_THIS_SESSION);
  }
}

void FocusApp::CreateMessageWindow(HINSTANCE instance) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = FocusApp::WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kMsgClass;
  RegisterClassExW(&wc);

  msg_hwnd_ = CreateWindowExW(
      0, kMsgClass, L"CALT Focus Tray Timer", WS_POPUP,
      0, 0, 0, 0, nullptr, nullptr, instance, this);
}

LRESULT CALLBACK FocusApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  FocusApp* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<FocusApp*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<FocusApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!self) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  return self->HandleMessage(hwnd, msg, wParam, lParam);
}

LRESULT FocusApp::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  // Tray is bound to main_hwnd_ (not the message-only window).
  if (hwnd == main_hwnd_ && tray_) {
    if (msg == TrayIcon::WM_TRAY || msg == WM_COMMAND) {
      tray_->HandleMessage(msg, wParam, lParam);
      return 0;
    }
  }
  if (hwnd == msg_hwnd_ && msg == WM_TIMER && wParam == kTipTimerId) {
    RefreshTrayTip();
    return 0;
  }
  if (hwnd == msg_hwnd_ && msg == WM_TIMER && wParam == kPushTimerId) {
    PushStatusTick();
    return 0;
  }
  if (msg == WM_FOCUS_SHELL) {
    switch (wParam) {
      case SHELL_RELOAD_UI:
        ReloadUi();
        return 0;
      case SHELL_ENSURE_TRAY:
        if (tray_) {
          tray_->EnsureAdded();
          tray_->ShowBalloon(
              L"CALT Focus",
              L"Tray icon refreshed. Win11: click ^ near the clock if still hidden.");
        }
        return 0;
      case SHELL_QUIT:
        Quit();
        return 0;
      case SHELL_OPEN_SETTINGS:
        OpenSettings();
        return 0;
      case SHELL_POPUP_TRAY:
        if (tray_) tray_->PopupMenu();
        return 0;
      case SHELL_RESTART_ENFORCER:
        RestartEnforcer();
        return 0;
      case SHELL_WAKE_BIBLE:
        WakeOpenBibleGate();
        return 0;
      default:
        break;
    }
  }
  if (hwnd == main_hwnd_ && msg == WM_WTSSESSION_CHANGE) {
    if (wParam == WTS_SESSION_UNLOCK || wParam == WTS_SESSION_LOGON) {
      WakeOpenBibleGate();
      return 0;
    }
  }
  if (g_taskbar_created && msg == g_taskbar_created) {
    if (tray_) {
      tray_->Destroy();
      tray_->EnsureAdded();
    }
    return 0;
  }

  switch (msg) {
    case WM_SIZE:
      if (webview_) {
        webview_->Resize();
      }
      return 0;
    case WM_CLOSE:
      // Hide to tray instead of quit
      ShowWindow(main_hwnd_, SW_HIDE);
      if (tray_) {
        tray_->EnsureAdded();
        tray_->ShowBalloon(L"CALT Focus", L"Still running in the tray. Right-click for menu.");
      }
      return 0;
    case WM_DESTROY:
      if (hwnd == main_hwnd_) {
        // only quit via tray Quit
        return 0;
      }
      break;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool FocusApp::BibleDoneTodayFromMirror() const {
  const std::wstring path = SoftlandPolicyPath();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 2 * 1024 * 1024) {
    CloseHandle(h);
    return false;
  }
  std::string body(static_cast<size_t>(sz.QuadPart), '\0');
  DWORD n = 0;
  const BOOL ok = ReadFile(h, body.data(), static_cast<DWORD>(body.size()), &n, nullptr);
  CloseHandle(h);
  if (!ok || n == 0) return false;
  body.resize(n);
  if (body.find("\"bible_done\":true") == std::string::npos &&
      body.find("\"bible_done\": true") == std::string::npos) {
    return false;
  }
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char today[16];
  sprintf_s(today, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
  const std::string needle = std::string("\"bible_done_for_date\":\"") + today + "\"";
  return body.find(needle) != std::string::npos;
}

void FocusApp::OpenBible() {
  NavigateShell(BibleUrl());
}

void FocusApp::WakeOpenBibleGate() {
  EnsureEnforcer();
  const bool bibleDone = BibleDoneTodayFromMirror();
  ShowFocusWindow(true);
  if (!bibleDone) {
    OpenBible();
    if (tray_) {
      tray_->ShowBalloon(L"CALT Focus",
                         L"Morning chapter first — read, then mark done.");
    }
  }
}

void FocusApp::ShowFocusWindow(bool maximize) {
  if (!main_hwnd_) {
    return;
  }
  if (webview_ && webview_->Ready()) {
    if (HasPrebuiltWebUi()) {
      if (!EnsurePrebuiltUiReady()) {
        ShowOfflinePage();
      }
    } else if (!PortListening(5173)) {
      ShowOfflinePage();
    }
  } else if (webview_failed_) {
    ShellExecuteW(nullptr, L"open", FocusUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return;
  }
  ShowWindow(main_hwnd_, maximize ? SW_SHOWMAXIMIZED : SW_SHOW);
  if (!maximize) {
    ShowWindow(main_hwnd_, SW_RESTORE);
  }
  SetForegroundWindow(main_hwnd_);
}

void FocusApp::ShowOfflinePage() {
  if (!webview_ || !webview_->Ready()) {
    return;
  }
  const wchar_t* html =
      L"data:text/html;charset=utf-8,"
      L"%3Chtml%3E%3Cbody%20style%3D'font-family:Segoe%20UI%2Csans-serif%3B"
      L"background%3A%230f172a%3Bcolor%3A%23e2e8f0%3Bpadding%3A32px'%3E"
      L"%3Ch2%3ECALT%20Focus%20%E2%80%94%20UI%20not%20ready%3C%2Fh2%3E"
      L"%3Cp%3EBuild%20precompiled%20UI%20once%20(no%20Vite%20daily)%3A%3C%2Fp%3E"
      L"%3Cpre%20style%3D'background%3A%231e293b%3Bpadding%3A12px'%3Enpm%20run%20build%3Afocus%3C%2Fpre%3E"
      L"%3Cp%3EThen%20reopen%20CALT%20Focus.%20SoftLand%2FArm%2FPlan%20run%20via%20enforcer%20%E2%80%94%20no%20Study%20API.%3C%2Fp%3E"
      L"%3C%2Fbody%3E%3C%2Fhtml%3E";
  webview_->Navigate(html);
}

bool FocusApp::EnsureFocusStaticServer() {
  if (PortListening(5174)) {
    return true;
  }
  const std::wstring root = RepoRoot();
  const std::wstring py = PythonExe();
  const std::wstring script = root + L"\\scripts\\serve_focus_ui.py";
  std::wstring cmdLine = L"\"" + py + L"\" \"" + script + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmdLine;
  const BOOL ok = CreateProcessW(
      nullptr,
      mutableCmd.data(),
      nullptr,
      nullptr,
      FALSE,
      CREATE_NO_WINDOW,
      nullptr,
      root.c_str(),
      &si,
      &pi);
  if (!ok) {
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return WaitForPort(5174, 20000);
}

bool FocusApp::EnsurePrebuiltUiReady() {
  if (!webview_ || !webview_->Ready() || !HasPrebuiltWebUi()) {
    return false;
  }
  // 1) WebView2 virtual host https://calt.app → dist-focus (ES modules work)
  if (webview_->MapStaticSite(DistDir())) {
    // Solo-pack: SoftLand / status JSON readable without :8000
    webview_->MapBehaviorData(DataBehaviorDir());
    webview_->MapBibleData(DataBibleDir());
    SetEnvironmentVariableW(L"CALT_FOCUS_UI_MODE", nullptr);
    return true;
  }
  // 2) Fallback: local static server (still no Vite)
  if (EnsureFocusStaticServer()) {
    SetEnvironmentVariableW(L"CALT_FOCUS_UI_MODE", L"static");
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"Using local prebuilt UI on :5174 (no Vite).");
    }
    return true;
  }
  return false;
}

bool FocusApp::PortListening(unsigned short port) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  // Non-blocking short connect
  u_long nonblock = 1;
  ioctlsocket(s, FIONBIO, &nonblock);
  const int r = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  bool ok = false;
  if (r == 0) {
    ok = true;
  } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
    fd_set w{};
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    ok = select(0, nullptr, &w, nullptr, &tv) > 0;
  }
  closesocket(s);
  return ok;
}

bool FocusApp::WaitForPort(unsigned short port, DWORD timeoutMs) {
  const DWORD start = GetTickCount();
  while (GetTickCount() - start < timeoutMs) {
    if (PortListening(port)) {
      return true;
    }
    Sleep(400);
    MSG pump{};
    while (PeekMessageW(&pump, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&pump);
      DispatchMessageW(&pump);
    }
  }
  return PortListening(port);
}

bool FocusApp::RunLifecycleCommand(const wchar_t* command, bool wait) {
  const std::wstring root = RepoRoot();
  const std::wstring py = PythonExe();
  const std::wstring script = root + L"\\scripts\\server_lifecycle.py";
  std::wstring cmdLine = L"\"" + py + L"\" \"" + script + L"\" ";
  cmdLine += command;

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmdLine;
  const BOOL ok = CreateProcessW(
      nullptr,
      mutableCmd.data(),
      nullptr,
      nullptr,
      FALSE,
      CREATE_NEW_CONSOLE,
      nullptr,
      root.c_str(),
      &si,
      &pi);
  if (!ok) {
    // Fallback: run.bat for full stack
    if (wcscmp(command, L"ensure-fast") == 0) {
      const std::wstring bat = root + L"\\run.bat";
      ShellExecuteW(nullptr, L"open", bat.c_str(), nullptr, root.c_str(), SW_SHOW);
      return true;
    }
    return false;
  }
  if (wait) {
    // Pump UI while waiting so tray stays responsive
    while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT) {
      MSG pump{};
      while (PeekMessageW(&pump, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pump);
        DispatchMessageW(&pump);
      }
    }
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

void FocusApp::EnsureEnforcer() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (scm) {
    SC_HANDLE svc = OpenServiceW(scm, L"CALTEnforcer", SERVICE_QUERY_STATUS | SERVICE_START);
    if (svc) {
      SERVICE_STATUS st{};
      if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_RUNNING) {
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return;
      }
      StartServiceW(svc, 0, nullptr);
      CloseServiceHandle(svc);
      CloseServiceHandle(scm);
      return;
    }
    CloseServiceHandle(scm);
  }

  const std::wstring root = RepoRoot();
  const std::wstring ps1 = root + L"\\scripts\\install\\install_enforcer_service.ps1";
  const std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -File \"" + ps1 + L"\" -Start";
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  sei.lpVerb = L"open";
  sei.lpFile = L"powershell.exe";
  sei.lpParameters = args.c_str();
  sei.nShow = SW_HIDE;
  if (ShellExecuteExW(&sei)) {
    if (sei.hProcess) {
      WaitForSingleObject(sei.hProcess, 15000);
      CloseHandle(sei.hProcess);
    }
    return;
  }

  const std::wstring console = root + L"\\scripts\\run\\run_native_enforcer_console.bat";
  ShellExecuteW(nullptr, L"open", console.c_str(), nullptr, root.c_str(), SW_SHOWMINNOACTIVE);
}

namespace {

void KillAllEnforcerProcesses() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"calt_enforcer.exe") == 0) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
        if (h) {
          TerminateProcess(h, 1);
          CloseHandle(h);
        }
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
}

bool StartEnforcerService() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = OpenServiceW(scm, L"CALTEnforcer", SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
  if (!svc) {
    CloseServiceHandle(scm);
    return false;
  }
  SERVICE_STATUS st{};
  if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_RUNNING) {
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    for (int i = 0; i < 40; ++i) {
      Sleep(100);
      if (!QueryServiceStatus(svc, &st) || st.dwCurrentState == SERVICE_STOPPED) break;
    }
  }
  const BOOL started = StartServiceW(svc, 0, nullptr);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return started == TRUE;
}

}  // namespace

void FocusApp::RestartEnforcer() {
  if (tray_) {
    tray_->ShowBalloon(L"CALT Focus", L"Restarting enforcer (desktop tracker)…");
  }

  // Prefer graceful service stop; then force-kill any leftover console instance.
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (scm) {
    SC_HANDLE svc = OpenServiceW(scm, L"CALTEnforcer", SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (svc) {
      SERVICE_STATUS st{};
      ControlService(svc, SERVICE_CONTROL_STOP, &st);
      for (int i = 0; i < 50; ++i) {
        Sleep(100);
        if (!QueryServiceStatus(svc, &st) || st.dwCurrentState == SERVICE_STOPPED) break;
      }
      CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
  }
  KillAllEnforcerProcesses();
  Sleep(400);

  const std::wstring lock = DataBehaviorDir() + L"\\enforcer_owner.lock";
  DeleteFileW(lock.c_str());

  bool ok = StartEnforcerService();
  if (!ok) {
    // Never bare-launch ProgramData exe — without CALT_DB it writes the wrong tree.
    // Console bat sets CALT_DB to this repo's productivity.db.
    const std::wstring console = RepoRoot() + L"\\scripts\\run\\run_native_enforcer_console.bat";
    if (GetFileAttributesW(console.c_str()) != INVALID_FILE_ATTRIBUTES) {
      ShellExecuteW(nullptr, L"open", console.c_str(), nullptr, RepoRoot().c_str(),
                    SW_SHOWMINNOACTIVE);
      ok = true;
    } else {
      EnsureEnforcer();
      ok = true;
    }
  }

  Sleep(1500);
  RefreshTrayTip();
  const EnforcerBadge b = ReadEnforcerBadge();
  if (tray_) {
    std::wstring body = L"Enforcer: ";
    body += b.LiveLabel();
    body += L" · Tracker: ";
    body += b.LiveLabel();
    if (!ok && !b.ok) {
      body = L"Restart attempted — still DOWN. Run install_native_enforcer.ps1 as Admin.";
    }
    tray_->ShowBalloon(L"CALT Focus", body.c_str());
  }
}

void FocusApp::StartWebStack() {
  if (tray_) {
    tray_->ShowBalloon(L"CALT Focus", L"Starting API (:8000) + Frontend (:5173)…");
  }
  RunLifecycleCommand(L"ensure-fast", true);
  const bool up = WaitForPort(5173, 90000);
  if (up) {
    OpenCalendar();
    if (tray_) {
      tray_->ShowBalloon(L"CALT Focus", L"Web stack is up — Calendar loaded.");
    }
  } else if (tray_) {
    tray_->ShowBalloon(
        L"CALT Focus",
        L"Still waiting on :5173. Check the server console, then Open Calendar.");
    ShowOfflinePage();
  }
  RefreshTrayTip();
}

void FocusApp::StartApiOnly() {
  if (tray_) {
    tray_->ShowBalloon(
        L"CALT Focus",
        L"Starting Study API (:8000) — optional; SoftLand/Arm already work offline…");
  }
  RunLifecycleCommand(L"ensure-api", true);
  WaitForPort(8000, 60000);
  RefreshTrayTip();
  if (tray_) {
    tray_->ShowBalloon(
        L"CALT Focus",
        PortListening(8000)
            ? L"Study API is up on :8000."
            : L"Study API not reachable yet — check console.");
  }
}

void FocusApp::StartFrontendOnly() {
  if (tray_) {
    tray_->ShowBalloon(L"CALT Focus", L"Starting Frontend (Vite :5173)…");
  }
  RunLifecycleCommand(L"ensure-frontend", true);
  const bool up = WaitForPort(5173, 90000);
  if (up) {
    OpenCalendar();
  } else {
    ShowOfflinePage();
  }
  RefreshTrayTip();
  if (tray_) {
    tray_->ShowBalloon(
        L"CALT Focus",
        up ? L"Frontend is up — Calendar loaded." : L"Frontend not up yet — check console.");
  }
}

void FocusApp::RunStack() {
  EnsureEnforcer();
  if (HasPrebuiltWebUi()) {
    OpenCalendar();
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"Enforcer ready — SoftLand/Arm/Plan offline. Study API is under Advanced.");
    }
    return;
  }
  StartWebStack();
}

void FocusApp::OpenProductivity() {
  NavigateShell(ProductivityUrl());
}

void FocusApp::OpenSettings() {
  NavigateShell(SettingsUrl());
}

void FocusApp::OpenCalendar() {
  NavigateShell(CalendarUrl());
}

void FocusApp::OpenPlan() {
  NavigateShell(PlanUrl());
}

void FocusApp::NavigateShell(const std::wstring& url) {
  if (HasPrebuiltWebUi()) {
    // Phase 7: Calendar/Plan/Settings open without auto-starting :8000.
    if (webview_ && webview_->Ready() && EnsurePrebuiltUiReady()) {
      // Same SPA document: only change the hash — full Navigate() reloads JS and feels slow.
      const size_t hashPos = url.find(L'#');
      if (hashPos != std::wstring::npos) {
        webview_->NavigateHash(url.substr(hashPos));
      } else {
        webview_->Navigate(url);
      }
      ShowWindow(main_hwnd_, SW_SHOW);
      SetForegroundWindow(main_hwnd_);
      return;
    }
  }
  if (!PortListening(5173)) {
    StartWebStack();
  }
  ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring FocusApp::SettingsUrlBusted() const {
  // Bust WebView module cache so rebuilt dist-focus JS is picked up.
  // Keep hash query intact: #/productivity?tab=settings&section=overview&_ui=…
  const DWORD t = GetTickCount();
  wchar_t buf[64]{};
  swprintf(buf, 64, L"%lu", static_cast<unsigned long>(t));
  std::wstring base = SettingsUrl();
  // SettingsUrl already has ?tab=settings — append bust + section.
  if (base.find(L"section=") == std::wstring::npos) {
    base += L"&section=overview";
  }
  return base + L"&_ui=" + buf;
}

std::wstring FocusApp::DocumentUrlBusted(const std::wstring& urlWithHash) const {
  // Put ?_ui= on the document URL (before #). Hash-only changes do not reload modules.
  const DWORD t = GetTickCount();
  wchar_t bust[48]{};
  swprintf(bust, 48, L"_ui=%lu", static_cast<unsigned long>(t));
  const size_t hashPos = urlWithHash.find(L'#');
  const std::wstring base =
      hashPos == std::wstring::npos ? urlWithHash : urlWithHash.substr(0, hashPos);
  const std::wstring hash = hashPos == std::wstring::npos ? L"" : urlWithHash.substr(hashPos);
  if (base.find(L'?') != std::wstring::npos) {
    return base + L"&" + bust + hash;
  }
  return base + L"?" + bust + hash;
}

void FocusApp::ReloadUi() {
  if (tray_) {
    tray_->ShowBalloon(L"CALT Focus", L"Reloading Focus UI…");
  }
  if (webview_ && webview_->Ready()) {
    EnsurePrebuiltUiReady();
    // Full Navigate — NavigateHash leaves stale dist-focus JS in memory after rebuilds.
    webview_->Navigate(DocumentUrlBusted(ProductivityUrl()));
    ShowWindow(main_hwnd_, SW_SHOW);
    SetForegroundWindow(main_hwnd_);
    return;
  }
  NavigateShell(SettingsUrlBusted());
}

bool FocusApp::RunNpmBuildFocus() {
  const std::wstring root = RepoRoot();
  // cmd.exe so npm.cmd resolves on PATH
  std::wstring cmdLine =
      L"cmd.exe /c \"npm run build:focus\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmdLine;
  const BOOL ok = CreateProcessW(
      nullptr,
      mutableCmd.data(),
      nullptr,
      nullptr,
      FALSE,
      CREATE_NO_WINDOW,
      nullptr,
      root.c_str(),
      &si,
      &pi);
  if (!ok) {
    return false;
  }
  // Keep tray alive while npm builds (can take ~30–60s)
  for (;;) {
    const DWORD wait = WaitForSingleObject(pi.hProcess, 200);
    MSG pump{};
    while (PeekMessageW(&pump, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&pump);
      DispatchMessageW(&pump);
    }
    if (wait == WAIT_OBJECT_0) {
      break;
    }
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
}

void FocusApp::UpdateUi() {
  if (tray_) {
    tray_->ShowBalloon(
        L"CALT Focus",
        L"Updating UI — npm run build:focus (may take a minute)…");
  }
  const bool ok = RunNpmBuildFocus();
  if (!ok) {
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"build:focus failed — check Node/npm in PATH, then try again.");
    }
    return;
  }
  if (!HasPrebuiltWebUi()) {
    if (tray_) {
      tray_->ShowBalloon(L"CALT Focus", L"dist-focus missing after build.");
    }
    return;
  }
  if (tray_) {
    tray_->ShowBalloon(L"CALT Focus", L"UI rebuilt — reloading…");
  }
  ReloadUi();
}

bool FocusApp::RunUpdateStackScript() {
  const std::wstring root = RepoRoot();
  const std::wstring script =
      root + L"\\scripts\\build\\update_calt_productivity.ps1";
  if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  // powershell -NoProfile -ExecutionPolicy Bypass -File "...\update_calt_productivity.ps1"
  std::wstring cmdLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"";
  cmdLine += script;
  cmdLine += L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmdLine;
  const BOOL ok = CreateProcessW(
      nullptr,
      mutableCmd.data(),
      nullptr,
      nullptr,
      FALSE,
      CREATE_NO_WINDOW,
      nullptr,
      root.c_str(),
      &si,
      &pi);
  if (!ok) {
    return false;
  }
  for (;;) {
    const DWORD wait = WaitForSingleObject(pi.hProcess, 200);
    MSG pump{};
    while (PeekMessageW(&pump, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&pump);
      DispatchMessageW(&pump);
    }
    if (wait == WAIT_OBJECT_0) {
      break;
    }
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
}

bool FocusApp::PendingUpdateNeedsRestart() const {
  const std::wstring focusNew =
      RepoRoot() + L"\\backend\\calt_focus\\build\\calt_focus.exe.new";
  const std::wstring enfNew =
      RepoRoot() + L"\\backend\\calt_enforcer\\build\\calt_enforcer.exe.new";
  if (GetFileAttributesW(focusNew.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return true;
  }
  if (GetFileAttributesW(enfNew.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return true;
  }
  const std::wstring pending =
      DataBehaviorDir() + L"\\pending_update.json";
  HANDLE h = CreateFileW(pending.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  char buf[4096]{};
  DWORD n = 0;
  const BOOL readOk = ReadFile(h, buf, sizeof(buf) - 1, &n, nullptr);
  CloseHandle(h);
  if (!readOk || n == 0) {
    return false;
  }
  buf[n] = 0;
  return strstr(buf, "\"needs_restart\": true") != nullptr ||
         strstr(buf, "\"needs_restart\":true") != nullptr;
}

void FocusApp::UpdateStack() {
  if (tray_) {
    tray_->ShowBalloon(
        L"CALT Focus",
        L"Updating stack — UI, classify rules, natives (may take several minutes)…");
  }
  const bool ok = RunUpdateStackScript();
  if (!ok) {
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"Update stack failed — see PowerShell / build logs. UI-only: use Update UI.");
    }
    return;
  }
  if (HasPrebuiltWebUi() && webview_ && webview_->Ready()) {
    ReloadUi();
  }
  if (PendingUpdateNeedsRestart()) {
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"UI/rules updated. Natives staged as .new — SoftLand off + Disarm, then Apply pending update && restart.");
    }
  } else if (tray_) {
    tray_->ShowBalloon(L"CALT Focus", L"Stack updated — UI reloaded.");
  }
}

void FocusApp::ApplyPendingUpdate() {
  if (FocusBlocksActive()) {
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"Turn SoftLand off and Disarm before applying a native update.");
    }
    return;
  }
  if (!PendingUpdateNeedsRestart()) {
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"No pending .new binaries. Run Update stack first (or UI already applied).");
    }
    return;
  }
  const std::wstring bat =
      RepoRoot() + L"\\scripts\\build\\apply_pending_update.bat";
  if (GetFileAttributesW(bat.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (tray_) {
      tray_->ShowBalloon(L"CALT Focus", L"apply_pending_update.bat missing.");
    }
    return;
  }
  if (tray_) {
    tray_->ShowBalloon(
        L"CALT Focus",
        L"Quitting to apply staged natives, then Focus will restart…");
  }
  // Detached: bat waits for Focus/enforcer exit, copies .new → exe, relaunches.
  ShellExecuteW(nullptr, L"open", bat.c_str(), nullptr, RepoRoot().c_str(), SW_SHOW);
  Quit();
}

void FocusApp::Quit() {
  // Solo-pack: tray Quit only when SoftLand off and Disarmed (else enforcer relaunches).
  if (FocusBlocksActive()) {
    if (tray_) {
      tray_->ShowBalloon(
          L"CALT Focus",
          L"Turn SoftLand off and Disarm hard-block before Quit (or unlock while armed).");
    }
    return;
  }
  WearableListenStop();
  if (tray_) {
    tray_->Destroy();
  }
  if (main_hwnd_) {
    DestroyWindow(main_hwnd_);
  }
  if (msg_hwnd_) {
    DestroyWindow(msg_hwnd_);
  }
  PostQuitMessage(0);
}

void FocusApp::RefreshTrayTip() {
  if (!tray_) {
    return;
  }
  const EnforcerBadge b = ReadEnforcerBadge();
  tray_->SetTooltip(FormatTrayTooltip(b).c_str());

  // Toast when enforcer records a new kill (apps SoftLand/hard-block).
  if (!b.last_kill_exe.empty()) {
    if (!kill_toast_primed_) {
      last_kill_toast_ = b.last_kill_exe;
      kill_toast_primed_ = true;
    } else if (b.last_kill_exe != last_kill_toast_) {
      last_kill_toast_ = b.last_kill_exe;
      std::wstring body = L"Closed: " + b.last_kill_exe + L" — hard block is armed.";
      tray_->ShowBalloon(L"CALT blocked an app", body.c_str());
    }
  }
}

namespace {

std::string ReadUtf8File(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string body = ss.str();
  // Trim BOM / whitespace so we can embed as a JSON value.
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) {
    body.pop_back();
  }
  if (!body.empty() && body.front() == '{') return body;
  return {};
}

}  // namespace

void FocusApp::PushStatusTick() {
  if (!webview_ || !webview_->Ready()) return;
  const std::string status = ReadUtf8File(EnforcerStatusPath());
  const std::string softland = ReadUtf8File(SoftlandPolicyPath());
  if (status.empty() && softland.empty()) return;
  const std::string fp = status + "\n" + softland;
  const bool unchanged = (fp == last_status_push_fp_);
  if (unchanged) {
    // Keepalive only — FE extends pushHealthy without applying SoftLand (no re-render).
    webview_->PostWebMessageJson("{\"type\":\"status_tick\",\"keepalive\":true}");
    return;
  }
  last_status_push_fp_ = fp;
  std::string msg = "{\"type\":\"status_tick\",\"enforcer_status\":";
  msg += status.empty() ? "null" : status;
  msg += ",\"softland\":";
  msg += softland.empty() ? "null" : softland;
  msg += "}";
  webview_->PostWebMessageJson(msg);
}

HICON FocusApp::MakeDotIcon() {
  const int s = 64;
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = s;
  bmi.bmiHeader.biHeight = -s;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC hdc = GetDC(nullptr);
  HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, hdc);
  if (!dib || !bits) {
    return LoadIcon(nullptr, IDI_APPLICATION);
  }
  auto* px = static_cast<unsigned char*>(bits);
  for (int y = 0; y < s; ++y) {
    for (int x = 0; x < s; ++x) {
      const int dx = x - 32;
      const int dy = y - 32;
      const int idx = (y * s + x) * 4;
      if (dx * dx + dy * dy <= 24 * 24) {
        px[idx + 0] = 166;  // B teal-ish
        px[idx + 1] = 184;
        px[idx + 2] = 20;
        px[idx + 3] = 255;
      } else {
        px[idx + 0] = 0;
        px[idx + 1] = 0;
        px[idx + 2] = 0;
        px[idx + 3] = 0;
      }
    }
  }
  HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmMask = mask;
  ii.hbmColor = dib;
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(dib);
  return icon ? icon : LoadIcon(nullptr, IDI_APPLICATION);
}
