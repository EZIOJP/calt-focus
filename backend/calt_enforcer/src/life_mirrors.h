#pragma once

#include <string>

/**
 * Maintain Life Tracker mirrors under behavior/ for Focus FE (calt-data reads).
 * Watch ingest still arrives via wearables hub → wearable_*.json; enforcer packs
 * life_today / life_days / life_history_14 so the UI never hammers :8765.
 */
void PublishLifeMirrors(const std::wstring& behaviorDir);
