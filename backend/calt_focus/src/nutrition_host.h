#pragma once

#include <string>

/** NutriNode routes served by CALT Focus on the LAN (/n uses these). */
struct NutriReply {
  int code = 500;
  std::string json;
};

NutriReply NutriHandle(const std::wstring& behaviorDir, const std::string& method,
                       const std::string& path, const std::string& body);
