#pragma once

#include <string>
#include <vector>

/** Telegram long-poll remote for Focus (status / SoftLand / power / optional chat). */

struct TelegramRemoteConfig {
  bool enabled = false;
  std::string bot_token;
  std::vector<long long> allowed_chat_ids;
  bool allow_chat = true;
  bool allow_power = true;
  /** Next inbound message from any chat is added to allowlist (then cleared). */
  bool link_next = false;
  long long offset = 0;
};

TelegramRemoteConfig TelegramRemoteLoad();
bool TelegramRemoteSave(const TelegramRemoteConfig& cfg, std::string* err);
/** Start/restart poller if enabled+token; stop otherwise. */
void TelegramRemoteApply(const TelegramRemoteConfig& cfg);
void TelegramRemoteStop();
bool TelegramRemoteIsPolling();
/** Status for WebView — token masked. */
std::string TelegramRemoteStatusJson();
/** Handle one text command (also used by unit-style tests). */
std::string TelegramRemoteDispatch(long long chatId, const std::string& text);
