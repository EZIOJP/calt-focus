#pragma once

#include <string>
#include <vector>

/** Device lock (Windows hosts porn block) — sole mutator lives in calt_enforcer. */

struct DeviceBlockSettings {
  int v = 1;
  bool enabled = false;
  bool block_porn = true;
  bool block_watch = false;
  bool block_social = false;
  std::vector<std::string> extra_domains;
  std::string source = "calt_enforcer";
};

struct DeviceBlockApplyResult {
  bool ok = false;
  bool needs_admin = false;
  bool applied = false;
  int domain_count = 0;
  std::string error;
  std::string doh_note;
  std::string verify_json;  // optional fragment
};

struct DeviceBlockStatusResult {
  DeviceBlockSettings settings;
  bool active = false;
  int configured_domain_count = 0;
  int managed_host_entries = 0;
  bool needs_sync = false;
  bool needs_admin_hint = false;
  std::string refresh;  // idle|running|ok|failed
  std::string refresh_error;
  std::string cache_as_of;
  std::string verify_json;
  std::string hosts_path;
};

/** Ensure productivity SoT exists; one-time migrate from data/behavior with marker. */
bool DeviceBlockEnsureMigrated(const std::wstring& behaviorDir);

bool DeviceBlockLoadSettings(const std::wstring& behaviorDir, DeviceBlockSettings& out);
bool DeviceBlockSaveSettings(const std::wstring& behaviorDir, const DeviceBlockSettings& in);

/**
 * Shared enable↔disable gate. confirm must be exact "DEVICE LOCK" (case-insensitive
 * trim) when wantEnabled != current.enabled. Returns false + err token on failure.
 */
bool DeviceBlockTrySetEnabled(const std::wstring& behaviorDir, bool wantEnabled,
                              const std::string& confirm, std::string* errOut);

/** Patch non-enable fields; enable flips go through TrySetEnabled. */
bool DeviceBlockPatchSettings(const std::wstring& behaviorDir, const DeviceBlockSettings& patch,
                              bool hasEnabled, bool hasPorn, bool hasWatch, bool hasSocial,
                              bool hasExtra, const std::string& confirm, std::string* errOut);

std::vector<std::string> DeviceBlockCollectDomains(const std::wstring& behaviorDir,
                                                   const DeviceBlockSettings& s);

DeviceBlockApplyResult DeviceBlockApplyHosts(const std::wstring& behaviorDir);
DeviceBlockApplyResult DeviceBlockRemoveHosts(const std::wstring& behaviorDir);

DeviceBlockStatusResult DeviceBlockStatus(const std::wstring& behaviorDir);

/** Kick async scrape worker; returns started|already_running|failed. */
std::string DeviceBlockRefreshListKick(const std::wstring& behaviorDir, bool force);

/** CLI / gateway JSON helpers */
std::string DeviceBlockStatusJsonExtra(const DeviceBlockStatusResult& st);
std::string DeviceBlockApplyJsonExtra(const DeviceBlockApplyResult& r);

/** One-shot CLI entry (no owner lock). Returns process exit code. */
int DeviceBlockCliMain(const std::wstring& behaviorDir, int argc, wchar_t** argv);
