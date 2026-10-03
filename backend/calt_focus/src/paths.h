#pragma once

#include <string>

// Repo / data helpers for calt_focus (no kills — UI only).

std::wstring ExeDir();
std::wstring RepoRoot();
std::wstring DataBehaviorDir();
std::wstring DataBibleDir();
std::wstring EnforcerStatusPath();
std::wstring SoftlandPolicyPath();
std::wstring DistDir();
bool HasPrebuiltWebUi();
std::wstring FocusUrl();
std::wstring ProductivityUrl();
std::wstring SettingsUrl();
std::wstring CalendarUrl();
std::wstring PlanUrl();
std::wstring WebViewUserDataDir();

// Virtual host used with WebView2 SetVirtualHostNameToFolderMapping(dist).
inline constexpr wchar_t kCaltVirtualHost[] = L"calt.app";
