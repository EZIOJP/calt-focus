#pragma once

#include <string>

/** Expire SoftLand clocks + apply due pending_changes; republish mirror if changed. */
bool TickSoftlandClocks(const std::wstring& behaviorDir);
