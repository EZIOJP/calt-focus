#pragma once
#include <string>

// Same format as backend.behavior.enforcer_ownership (pid\\nts\\n).
void ClaimOwnerLock(const std::wstring& lockPath);
void RefreshOwnerLock(const std::wstring& lockPath);
void ReleaseOwnerLock(const std::wstring& lockPath);
