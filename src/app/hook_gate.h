#pragma once
#include "agent/primitives.h"
#include "app/hook_config.h"
#include "app/hook_runner.h"
#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"
#include "tools/exec.h"

// pi's `beforeToolCall` seam; runs on the caller's thread, unlike HookRunner.
struct HookGate;

// Null when no PreToolUse command exists. `config` must outlive the gate.
HookGate* HookGate_Create(Arena* arena, const HookConfig* config,
                          const HookSessionInfo* session, volatile i32* abort);

// Safe to call while an evaluation is in flight.
void HookGate_SetSession(HookGate* gate, const HookSessionInfo* session);

// A null `user` allows, so the seam can be installed unconditionally.
void HookGate_BeforeTool(void* user, Arena* arena, StrView toolName, StrView toolCallId,
                         const ToolArgs* args, AgentToolGateDecision* out);

// Unknown answers (spawn failure, timeout, unreadable output) block. Strings
// are copied into `arena`; `result->Output` may live in scratch.
void HookGate_Decide(Arena* arena, const ExecResult* result, AgentToolGateDecision* out);
