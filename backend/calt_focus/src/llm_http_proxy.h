#pragma once

#include <string>

/** Native HTTP for Focus LLM hosts — bypasses WebView CORS to OpenJarvis/Ollama. */

struct LlmHttpResult {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
  bool is_base64 = false;
};

/** Allow localhost OpenJarvis/Ollama + OpenRouter + NVIDIA endpoints. */
bool LlmHttpUrlAllowed(const std::string& url);

LlmHttpResult LlmHttpRequest(const std::string& method, const std::string& url,
                             const std::string& authorization, const std::string& contentType,
                             const std::string& body, unsigned timeoutMs, bool base64Response = false);
