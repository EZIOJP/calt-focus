#pragma once

#include <string>
#include <vector>

/** Close user apps from Focus session (taskkill). Never kills OS/Focus/enforcer. */

struct PcAppsResult {
  bool ok = false;
  int closed = 0;
  std::string detail;
};

/** Close common browsers (Edge/Chrome/Firefox/Brave/Opera) — not WebView2 host. */
PcAppsResult PcAppsCloseBrowsers();
/** Close named .exe list (comma/space). Protected names are skipped. */
PcAppsResult PcAppsCloseNamed(const std::vector<std::string>& exes);
std::string PcAppsResultJson(const PcAppsResult& r);
