#pragma once

#include <string>

/** Piper (C++ / ONNX) voices for Qwen. No Python. */
struct SpeechSnap {
  bool installed = false;
  bool ready = false;
  std::string qwenVoice;
  std::string normalVoice;
  std::string detail;
};

struct SpeechReply {
  int code = 500;
  std::string contentType = "application/json";
  std::string body;
};

void SpeechPrepare(const std::wstring& repoRoot);
SpeechSnap SpeechStatus();

/** GET /api/speech/status, POST /api/speech {text, voice} → audio/wav. */
SpeechReply SpeechHandle(const std::string& method, const std::string& path, const std::string& body);
