#pragma once

#include "status_writer.h"

#include <string>

struct FocusWatchState {
  bool softland_enabled = false;
  bool focus_running = false;
  bool softland_or_armed = false;
  unsigned focus_relaunch_count = 0;
  std::string focus_last_relaunch_at;  // ISO UTC or empty
  bool focus_relaunch_suppressed = false;
};

/** Read softland_enabled from data/behavior/softland_policy.json (next to DB). */
bool ReadSoftlandEnabled(const std::wstring& dbPath, bool* out_enabled);

/** True if a process with this basename is running (case-insensitive). */
bool ProcessBasenameRunning(const wchar_t* exeBase);

/** Resolve calt_focus.exe: CALT_FOCUS_EXE, sibling of enforcer, or CALT_REPO. */
std::wstring ResolveFocusExePath();

/**
 * If SoftLand on OR armed and Focus missing → relaunch with backoff (max 3 / 60s).
 * Updates watch fields for enforcer_status.json.
 */
void TickFocusWatchdog(bool armed, bool softland_enabled, FocusWatchState& state);
