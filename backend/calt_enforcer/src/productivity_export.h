#pragma once

#include <string>

/**
 * Focus-native Settings export — writes behavior/productivity_export.{json,csv}
 * so the WebView can download via calt-data without Study :8000.
 * On success returns "" and fills *extraOut with JSON fields (files, range, …).
 */
std::string ProductivityExportHandle(const std::wstring& behaviorDir, const std::wstring& dbPath,
                                     const std::string& payloadJson, std::string* extraOut);

/** Lightweight day markers for ExportRangeCalendar (presence dots). */
std::string ProductivityExportPresenceHandle(const std::wstring& behaviorDir,
                                             const std::wstring& dbPath,
                                             const std::string& payloadJson,
                                             std::string* extraOut);
