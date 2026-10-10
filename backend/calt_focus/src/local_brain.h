#pragma once

#include <string>

/** llama.cpp server hosting a local GGUF (default Qwen2.5-1.5B-Instruct Q4_K_M). */
struct BrainSnap {
  bool installed = false;
  bool running = false;
  bool ready = false;
  int port = 8099;
  int ctx = 4096;
  std::string model;
  std::string detail;
};

struct BrainReply {
  int code = 500;
  std::string json;
};

bool LocalBrainStart(const std::wstring& repoRoot);
void LocalBrainStop();
BrainSnap LocalBrainStatus();

/** One short completion. False when the server is not ready or the reply is empty. */
bool LocalBrainComplete(const std::string& system, const std::string& user, int maxTokens,
                        std::string* text, std::string* err);

BrainReply LocalBrainHandle(const std::string& method, const std::string& path,
                            const std::string& body);
