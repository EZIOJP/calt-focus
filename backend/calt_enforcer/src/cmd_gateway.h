#pragma once

#include <string>

void CmdGatewayInit();
/** Serve one poll of the pipe pool (used by the background gateway thread). */
void CmdGatewayPoll(const std::wstring& behaviorDir);
void CmdGatewayShutdown();

/**
 * Dedicated thread so SoftLand/Bible/SQLite work cannot starve ConnectNamedPipe.
 * Call after ProductivityStoreOpen + CmdGatewayInit. Shutdown joins the thread.
 */
void CmdGatewayStartBackground(const std::wstring& behaviorDir);
void CmdGatewayStopBackground();

/** Heartbeat for enforcer_status — true when the gateway thread is alive. */
bool CmdGatewayThreadAlive();
/** GetTickCount of last successful pipe request (0 = none yet). */
unsigned long CmdGatewayLastOkTick();
/** Listening pipe slots currently armed (0..pool). */
int CmdGatewayListeningSlots();

/**
 * Run a gateway op without a pipe client — used by the clock tick to apply due
 * pending_changes rows. Returns "" on success, else an error token.
 */
std::string GatewayApplyOp(const std::string& op, const std::string& payloadJson,
                           const std::wstring& behaviorDir);
