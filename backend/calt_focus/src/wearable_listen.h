#pragma once

#include <string>

/**
 * LAN listener for CALT Sync.
 * The watch reaches the phone over BLE; the phone POSTs JSON to this process.
 * The body is spooled and applied by calt_enforcer (wearable.ingest) — not the Python hub.
 * Returns false when the port is already taken.
 */
bool WearableListenStart(const std::wstring& behaviorDir);
void WearableListenStop();
