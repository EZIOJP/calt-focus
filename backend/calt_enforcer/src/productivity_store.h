#pragma once

#include <string>
#include <vector>

struct sqlite3;
/** Exposed for life_content.cpp (same process DB handle). */
extern sqlite3* gDb;

/** SoftLand SoT row — full policy document JSON + extracted clocks for tick. */
struct ProductivitySoftland {
  int schema_version = 1;
  std::string document_json;  // full softland_policy.json body (SoT payload)
  std::string updated_at;
  // Cached from document for tick / commands (kept in sync on save)
  bool softland_enabled = false;
  std::string free_until;
  std::string incubation_until;
  bool reward_day_active = false;
  int earned_ledger_seconds = 0;
};

struct ProductivityGatewayMeta {
  unsigned gateway_seq = 0;
  std::string last_publish_at;
};

/** Open vocab DB (WAL + busy_timeout). Call once per process. */
bool ProductivityStoreOpen(const std::wstring& dbPath);

void ProductivityStoreClose();

/** Ensure DDL; import softland_policy.json if row missing. */
bool ProductivityMigrateAndImport(const std::wstring& softlandJsonPath);

bool ProductivityLoadSoftland(ProductivitySoftland& out);
bool ProductivitySaveSoftland(const ProductivitySoftland& in);

unsigned ProductivityBumpSeq();
bool ProductivityLoadMeta(ProductivityGatewayMeta& out);

/** Refresh cached clock fields from document_json. */
void ProductivityRefreshCacheFromDocument(ProductivitySoftland& s);

/** Apply cache fields back into document_json (minimal JSON rewrite). */
bool ProductivityApplyCacheToDocument(ProductivitySoftland& s);

/**
 * Replace the value for `key` in `doc` with `rawValueJson` (object, array,
 * string, number, bool or null). Returns false when the key is absent.
 */
bool ProductivityReplaceJsonValue(std::string& doc, const char* key,
                                  const std::string& rawValueJson);

/**
 * Re-import the JSON mirror into the SQLite SoT when the file's updated_at is
 * newer than the stored row (Python writers, out-of-band edits). Returns true
 * when an import happened.
 */
bool ProductivityImportIfStale(const std::wstring& softlandJsonPath);

struct ProductivityPendingRow {
  long long id = 0;
  std::string op;
  std::string payload_json;
};

/** Pending rows whose apply_after has passed (status='pending'). */
bool ProductivityDuePending(const std::string& nowIsoLocal,
                            std::vector<ProductivityPendingRow>& out);

bool ProductivityMarkPending(long long id, const char* status);

bool ProductivityInsertPending(const std::string& applyAfterIso, const std::string& op,
                               const std::string& payloadJson);

/** Append a ledger row (earn | spend | adjust | reward_day). */
bool ProductivityLedgerAdd(const char* kind, int seconds, const std::string& note,
                           const char* source);

/** Recent ledger rows as a JSON array string (newest first). */
std::string ProductivityLedgerRecentJson(int limit);

/** Pending (not yet applied) change rows as a JSON array string. */
std::string ProductivityPendingListJson(int limit);

std::wstring ProductivityBehaviorDirFromDb(const std::wstring& dbPath);

// ---------------------------------------------------------------------------
// P5a — unlock accounting (day passes, reward credits, per-day earn events).
// Local calendar dates ("YYYY-MM-DD"); weeks are Monday-start local.
// ---------------------------------------------------------------------------

std::string ProductivityLocalDate();
std::string ProductivityWeekStart();

int ProductivityPassesUsedThisWeek();
bool ProductivityPassGrantedToday();
bool ProductivityInsertPass(const std::string& localDate, const std::string& weekStart);

/** `kind` is "qualified" or "used". */
int ProductivityRewardCount(const char* kind);
int ProductivityRewardGranted();
bool ProductivityRewardSetGranted(int granted);
bool ProductivityRewardMark(const char* kind, const std::string& localDate);

/** False when the event has not fired on `localDate` yet. */
bool ProductivityDayEventSeconds(const std::string& localDate, const std::string& event,
                                 int* outSeconds);
int ProductivityDayEarnedSecondsToday();
bool ProductivityInsertDayEvent(const std::string& localDate, const std::string& event,
                                int earnedSeconds);

/** Incubation starts recorded in the ledger at or after `sinceIso` (rate limit). */
int ProductivityIncubationStartsSinceIso(const std::string& sinceIso);

/** One-time import of data/bible/reward_days_*.json + day_passes_*.json. */
void ProductivityImportLegacyUnlockHistory(const std::wstring& dataDir);

// ---------------------------------------------------------------------------
// Phase 6a — planner SoT (planner_blocks / planner_routines in vocab_app.db).
// Claims existing Python tables; Focus mutates only via gateway.
// ---------------------------------------------------------------------------

/** Ensure planner_* DDL + one-time ownership claim (existing rows stay). */
void ProductivityEnsurePlanner();

/** Blocks overlapping [fromIso, toIso) for user (default user_id=1). */
std::string ProductivityPlanListJson(const std::string& fromIso, const std::string& toIso,
                                     int userId = 1);

std::string ProductivityPlanGetJson(long long id, int userId = 1);

/**
 * Create (no id) or update (id set). Payload fields mirror Focus plannerClient.
 * On success writes block JSON into *outBlockJson and bumps gateway_seq.
 */
bool ProductivityPlanUpsert(const std::string& payloadJson, int userId, std::string* outBlockJson);

bool ProductivityPlanDelete(long long id, int userId = 1);

std::string ProductivityRoutineListJson(int userId = 1);

bool ProductivityRoutineUpsert(const std::string& payloadJson, int userId,
                               std::string* outRoutineJson);

bool ProductivityRoutineDelete(long long id, int userId = 1);

/** Phase restore: start / complete / roll-forward / apply routines. */
bool ProductivityPlanStart(long long id, int userId, std::string* outBlockJson);
bool ProductivityPlanComplete(long long id, int userId, int minutesSpentOrNeg1,
                              std::string* outBlockJson);
/** Marks block rolled + creates new scheduled block; writes both JSON objects. */
bool ProductivityPlanRollForward(long long id, int userId, const std::string& newStartIsoOrEmpty,
                                 std::string* outRolledJson, std::string* outNewJson);
/** Apply enabled routines for local date YYYY-MM-DD (empty = today). Returns created count. */
int ProductivityRoutineApply(int userId, const std::string& dateYmdOrEmpty, bool skipOverlaps);

/** Overlay sessions from tracked_sessions for Focus (no Study API). */
std::string ProductivityPlanOverlayJson(const std::string& fromIso, const std::string& toIso,
                                        int userId = 1);

/** Simple day adherence summary JSON object for Focus. */
std::string ProductivityPlanAdherenceJson(const std::string& dayYmd, int userId = 1);
