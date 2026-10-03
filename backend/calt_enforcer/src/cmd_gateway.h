#pragma once

#include <string>

void CmdGatewayInit();
void CmdGatewayPoll(const std::wstring& behaviorDir);
void CmdGatewayShutdown();

/**
 * Run a gateway op without a pipe client — used by the clock tick to apply due
 * pending_changes rows. Returns "" on success, else an error token.
 */
std::string GatewayApplyOp(const std::string& op, const std::string& payloadJson,
                           const std::wstring& behaviorDir);
