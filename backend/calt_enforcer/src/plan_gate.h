#pragma once

#include <string>

/**
 * Phase 6b: map the active planner block → SoftLand clocks (free_until / plan_block
 * mirror). Call from softland tick and gateway op plan.apply_gate.
 * Returns true when SoftLand SoT/mirror changed.
 */
bool ApplyActivePlanToSoftland(const std::wstring& behaviorDir, int userId = 1);
