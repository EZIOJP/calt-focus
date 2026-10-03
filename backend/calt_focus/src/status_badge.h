#pragma once

#include <string>

// Best-effort read of enforcer_status.json (UI badge only — no kills).

struct EnforcerBadge {
  bool owns = false;
  bool armed = false;
  bool softland_or_armed = false;
  bool service_running = false;
  std::wstring last_kill_exe;
  std::wstring updated_at;  // ISO UTC from mirror
  bool ok = false;
  /** Status file readable and updated_at within ~45s (or missing age treated stale). */
  bool Fresh() const;
  /** owns + fresh => LIVE; ok but stale => STALE; else DOWN. */
  const wchar_t* LiveLabel() const;
};

EnforcerBadge ReadEnforcerBadge();
/** SoftLand on or hard-block armed — Focus Quit is refused while true. */
bool FocusBlocksActive();
std::wstring FormatTrayTooltip(const EnforcerBadge& b);
