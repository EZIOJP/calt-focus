#pragma once

#include <string>

/** Ensure productivity_day_tasks DDL. */
void DayLoopEnsureSchema();

/** JSON array of tasks for local date (YYYY-MM-DD; empty = today). */
std::string DayLoopTaskListJson(const std::string& dateYmdOrEmpty, int userId = 1);

bool DayLoopTaskUpsert(const std::string& payloadJson, int userId, std::string* outTaskJson);
bool DayLoopTaskSetDone(long long id, bool done, int userId);
bool DayLoopTaskDelete(long long id, int userId);

/**
 * Confirm morning plan: SoftLand ON + Arm ON (study day).
 * Requires bible_done_for_date == today. Seeds kill list (incl. cursor until goal).
 * No ledger credit. Returns error token or empty; *extraOut gateway fragment.
 */
std::string DayLoopConfirmPlan(const std::wstring& behaviorDir, std::string* extraOut);

/**
 * Landing / close-out snapshot: planned, tracked, tasks, free, bedtime, mode hints.
 * Writes JSON object fields into *extraOut (leading comma form for gateway).
 */
std::string DayLoopSnapshot(const std::wstring& behaviorDir, const std::wstring& dbPath,
                            std::string* extraOut);

/**
 * Dual gate: all tasks done (or zero tasks) AND tracked ≥ 50% planned → grant 1h free once/day.
 * Returns error or empty; *extraOut may include grant info.
 */
std::string DayLoopEvaluateClose(const std::wstring& behaviorDir, const std::wstring& dbPath,
                                 std::string* extraOut);

/** Set runtime.bedtime_hm / wake_hm. Payload: bedtime_hm, optional wake_hm. */
std::string DayLoopSetBedtime(const std::wstring& behaviorDir, const std::string& payload);

/** Tick: bedtime_active; goal-met EOD free; optional dual-gate evaluate_close. */
bool DayLoopTick(const std::wstring& behaviorDir, const std::wstring& dbPath);

/** True when SoftLand goals.bible_done + bible_done_for_date == today. */
bool DayLoopBibleDoneToday(const std::string& softlandDocumentJson);

/** Drop study-temp kills (cursor.exe) after free/reward/pass; keep games/social. */
bool DayLoopClearStudyTempKills(const std::wstring& behaviorDir);

/**
 * Emergency wind-down: confirm EMERGENCY → emergency_until = now+minutes (default 30).
 * SoftLand decide treats as free for non-distraction hosts while active.
 */
std::string DayLoopEmergencyWinddown(const std::wstring& behaviorDir, const std::string& payload);

/**
 * Import unfinished goal blocks from from_date into today (shift start to now+slots).
 * Payload: from_date YYYY-MM-DD. Skips routine-like categories food/sleep/commute if present.
 */
std::string DayLoopImportFromDate(const std::string& payload, int userId, std::string* extraOut);
