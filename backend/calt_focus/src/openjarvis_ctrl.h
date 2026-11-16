#pragma once

#include <string>

/** Background OpenJarvis `jarvis start` (:8000) — no desktop GUI required. */

struct OpenJarvisHostStatus {
  bool reachable = false;
  unsigned long pid = 0;
  std::string jarvis_exe;
  std::string model;
  std::string error;
};

OpenJarvisHostStatus OpenJarvisHostProbe();
/** Start daemon if down. model e.g. qwen3.5:2b (empty = default). */
bool OpenJarvisHostEnsure(const std::string& model, std::string* err);
bool OpenJarvisHostStop(std::string* err);
std::string OpenJarvisHostStatusJson(const OpenJarvisHostStatus& st);
