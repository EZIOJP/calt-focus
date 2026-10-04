#pragma once

#include <string>

/** Expire SoftLand clocks + apply due pending_changes; republish mirror if changed. */
/** Fast path (~kill cadence): free/incubation expiry + pending ops. */
bool TickSoftlandClocks(const std::wstring& behaviorDir);

/**
 * Slow path (~1 min): planner → SoftLand + day-loop (bedtime/close-out).
 * Kill loop stays realtime elsewhere; this is calendar/DB sync only.
 */
bool TickSoftlandPlanAndDayLoop(const std::wstring& behaviorDir);
