#pragma once

#include <string>

/**
 * Store one CALT Sync dump into behavior mirrors.
 * payload is either {"spool":"<utf-8 path under behavior/wearable_inbox>"} or the watch object.
 * Returns "" and a leading-comma JSON fragment on success, else an error token.
 */
std::string WearableIngest(const std::wstring& behaviorDir, const std::string& payload,
                           std::string* extraOut);

/** Leading-comma fragment: last_received_at, received, watch_received, last_source. */
void WearableStatusExtra(const std::wstring& behaviorDir, std::string* extraOut);
