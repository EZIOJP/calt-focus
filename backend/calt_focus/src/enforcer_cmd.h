#pragma once

#include <string>

/** Send one message to calt_enforcer named pipe; returns response JSON. */
bool EnforcerSendCommand(const std::string& jsonReq, std::string& jsonResp,
                         unsigned timeoutMs = 4000);
