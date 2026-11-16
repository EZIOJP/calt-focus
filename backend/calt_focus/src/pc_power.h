#pragma once

#include <string>

/** Interactive-session PC power actions (Focus UI process — not enforcer service). */

enum class PcPowerOp { Lock, Sleep, Shutdown, Cancel };

struct PcPowerResult {
  bool ok = false;
  std::string detail;
};

PcPowerResult PcPowerRun(PcPowerOp op);
/** Delayed shutdown; seconds clamped to 0..86400. Requires confirm at call site. */
PcPowerResult PcPowerShutdownIn(unsigned seconds);
std::string PcPowerResultJson(const PcPowerResult& r);
