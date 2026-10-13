#pragma once

#include <string>

struct GateWatchTickResult {
  int killed = 0;
  std::wstring last_exe;
  unsigned long last_pid = 0;
  bool candidate = false;
  int strikes = 0;
};

/**
 * SoftLand companion: close msedge.exe when Gate/msg_host heartbeat is dead.
 * Never runs while SoftLand is off, free/reward is active, or during startup grace.
 * Does not touch Arm / hard_block_armed. Leaves msedgewebview2 (Focus) alone.
 */
GateWatchTickResult TickGateWatch(const std::wstring& behaviorDir);
