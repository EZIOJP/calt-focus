#pragma once

#include "productivity_store.h"

#include <string>

/** Atomically write softland_policy.json from SoT document. */
bool PublishSoftlandMirror(const std::wstring& behaviorDir, const ProductivitySoftland& s);

/** Atomically write any behavior/<name> UTF-8 mirror (Focus FE reads via calt-data). */
bool WriteBehaviorMirrorFile(const std::wstring& behaviorDir, const wchar_t* fileName,
                             const std::string& body);

/** day.status projection → day_status.json (pass/reward/ledger; no pipe on load). */
bool PublishDayStatusMirror(const std::wstring& behaviorDir);

/** Planner ±14d blocks + routines → plan_blocks.json. */
bool PublishPlanBlocksMirror(const std::wstring& behaviorDir);

/** Batch: day_loop + day_status + journal + bible + plan mirrors for Focus reads. */
void PublishFocusReadMirrors(const std::wstring& behaviorDir);
