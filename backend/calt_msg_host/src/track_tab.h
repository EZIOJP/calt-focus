#pragma once

#include <string>

/** Insert/update a browser tab session into tracked_sessions (WAL + busy_timeout). */
bool TrackTabToSqlite(const std::string& url, const std::string& title, const std::string& domain);

std::wstring ResolveCaltDbPathW();
