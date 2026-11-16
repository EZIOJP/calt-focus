#pragma once

#include <string>

// Start / stop / probe calt_sync_host (:8765) from Focus UI. Not the enforcer.

struct SyncHostStatus {
  bool running = false;
  bool reachable = false;
  std::string base_url;
  std::string token;
  unsigned long pid = 0;
  std::string error;
};

SyncHostStatus SyncHostProbe();
bool SyncHostStart(std::string* err);
bool SyncHostStop(std::string* err);
std::string SyncHostStatusJson(const SyncHostStatus& st);
