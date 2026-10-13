#pragma once

#include <string>

/** Math→ML roadmap progress in productivity.db (Focus SoT). */
void RoadmapEnsureTables();

/** Full progress document JSON, or "{}" when empty. */
std::string RoadmapGetDocumentJson(int userId = 1);

/** Upsert progress document + write behavior/jp_roadmap.json mirror. */
bool RoadmapSaveDocument(int userId, const std::string& documentJson,
                         const std::wstring& behaviorDir);

/** Append one graded attempt for auditing. */
bool RoadmapLogAttempt(int userId, const std::string& topicId, int level, bool correct,
                       int timeMs, const std::string& mode, const std::string& prompt);

/** Newest attempts as JSON array. */
std::string RoadmapRecentAttemptsJson(int limit, int userId = 1);

/** Compact summary for mirror / status chips. */
std::string RoadmapSummaryJson(int userId = 1);
