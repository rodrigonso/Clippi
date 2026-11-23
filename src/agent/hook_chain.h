#pragma once

#include "agent/hooks.h"

// Links run in registration order; the first block or failure stops a hook,
// except AfterTurn, which runs every link. PrepareNextTurn merges per field.

constexpr u32 AGENT_HOOK_CHAIN_CAPACITY = 8;

struct AgentHookChain {
  AgentHarnessHooks Links[AGENT_HOOK_CHAIN_CAPACITY];
  u32 Count;
};

bool AgentHookChain_Add(AgentHookChain* chain, const AgentHarnessHooks* hooks);

// With two or more links, `chain` must outlive every harness configured with it.
AgentHarnessHooks AgentHookChain_Compose(AgentHookChain* chain);
