#pragma once

#include "agent/harness.h"
#include "tui/tui.h"

// `user` is a Tui*.
void TuiBridge_HarnessEvent(void* user, const AgentHarnessEvent* ev);

TuiPermissionMode TuiBridge_ToTuiPermissionMode(AgentPermissionMode mode);
AgentPermissionMode TuiBridge_FromTuiPermissionMode(TuiPermissionMode mode);

TuiAgentHooks TuiBridge_AgentHooks(AgentHarness* harness);
