#pragma once

#include <string>

struct AssistantReply {
  int code = 500;
  std::string json;
};

/** Coach routes. The local model reads the day and may bend a situation, not Arm. */
AssistantReply AssistantHandle(const std::wstring& behaviorDir, const std::string& method,
                               const std::string& path, const std::string& body);
