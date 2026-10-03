#pragma once

#include <string>

/** P5b: classify sessions + publish data/behavior/day_rollup.json for Focus mirrors. */
void TickDayRollup(const std::wstring& dbPath, const std::wstring& behaviorDir);
