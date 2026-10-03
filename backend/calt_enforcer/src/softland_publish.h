#pragma once

#include "productivity_store.h"

#include <string>

/** Atomically write softland_policy.json from SoT document. */
bool PublishSoftlandMirror(const std::wstring& behaviorDir, const ProductivitySoftland& s);
