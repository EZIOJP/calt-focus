#pragma once
#include <string>

// Session-0 (LocalSystem service) cannot read interactive FG / last-input.
// Spawn calt_enforcer.exe --tracker-agent in the console user session.
void TrackerAgentEnsure(const std::wstring& dbPath);
void TrackerAgentStop();

// Returns true when this process is Session 0 (needs the agent for desktop tracking).
bool TrackerNeedsUserSessionAgent();

// --tracker-agent entry: FG session writer only (no owner lock / gateway).
int RunTrackerAgentLoop(const std::wstring& dbPath);
