#pragma once

#include "agent/hooks.h"
#include "agent/tool.h"
#include "base/thread.h"
#include "llm/types.h"
#include "session/replay.h"

struct AgentPermissionGate;

enum class ToolExecution : u8 { Parallel, Sequential };

struct AgentProviderRequest {
  LlmContext Context;
  ToolDef* ToolDefs;
};

void AgentProvider_BuildRequest(Arena* arena, StrView systemPrompt,
                                const Message* messages, u32 messageCount,
                                const Tool* tools, u32 toolCount,
                                AgentProviderRequest* out);

typedef bool (*AgentResolveAuthFn)(void* user, Arena* arena, StrView provider,
                                   volatile i32* abort, ProviderAuth* out);

struct AgentProviderAttemptInput {
  Arena* Mem;
  Provider ProviderImpl;
  const Model* ModelInfo;
  const LlmContext* Context;
  StreamOptions Options;
  StreamSink Sink;
  void* AuthUser;
  AgentResolveAuthFn ResolveAuth;
};

struct AgentProviderAttemptResult {
  Message Assistant;
  bool Ok;
};

AgentProviderAttemptResult AgentProvider_RunAttempt(
    const AgentProviderAttemptInput* input);

struct AgentRetryPolicy {
  bool Enabled;
  u32 MaxRetries;
  u32 BaseDelayMs;
  u32 MaxDelayMs;
};

// Matches pi: agent-level retries on, transport retries off.
constexpr u32 AGENT_RETRY_DEFAULT_MAX_RETRIES = 3;
constexpr u32 AGENT_RETRY_DEFAULT_BASE_DELAY_MS = 2000;
constexpr u32 AGENT_RETRY_DEFAULT_MAX_DELAY_MS = 8000;

inline AgentRetryPolicy AgentRetryPolicy_Default() {
  AgentRetryPolicy policy = {};
  policy.Enabled = true;
  policy.MaxRetries = AGENT_RETRY_DEFAULT_MAX_RETRIES;
  policy.BaseDelayMs = AGENT_RETRY_DEFAULT_BASE_DELAY_MS;
  policy.MaxDelayMs = AGENT_RETRY_DEFAULT_MAX_DELAY_MS;
  return policy;
}

struct AgentRetryDecision {
  bool Retry;
  u32 DelayMs;
};

// Deliberate divergence: pi has no turn cap. Steering and follow-up reset the
// count; zero MaxTurns restores pi's behaviour.
struct AgentTurnLimitPolicy {
  bool Enabled;
  u32 MaxTurns;
};

constexpr u32 AGENT_TURN_LIMIT_DEFAULT_MAX_TURNS = 100;

inline AgentTurnLimitPolicy AgentTurnLimitPolicy_Default() {
  AgentTurnLimitPolicy policy = {};
  policy.Enabled = true;
  policy.MaxTurns = AGENT_TURN_LIMIT_DEFAULT_MAX_TURNS;
  return policy;
}

bool AgentTurnLimitPolicy_IsValid(const AgentTurnLimitPolicy* policy);

// Separate from AgentCheckpoint_* so pi's continuation logic stays untouched.
bool AgentTurnLimit_Reached(const AgentTurnLimitPolicy* policy, u32 turnsUsed);

AgentRetryDecision AgentProvider_RetryDecision(const AgentRetryPolicy* policy,
                                               u32 attemptIndex,
                                               bool providerOk,
                                               const Message* assistant);
bool AgentProvider_SleepBackoff(u32 delayMs, volatile i32* abort);

struct AgentToolEventSink {
  void* User;
  void (*OnUpdate)(void* user, StrView toolCallId, StrView toolName,
                   const ToolResult* partial);
  void (*OnEnd)(void* user, StrView toolCallId, StrView toolName,
                const ToolResult* result, bool isError);
};

struct AgentToolDispatchGate {
  void* User;
  bool (*TryStart)(void* user);
};

struct AgentPreparedTool {
  const Content* Call;
  const ToolRuntime* Runtime;
  const Tool* Resolved;
  ToolArgs Args;
  StrView EffectiveArgumentsJson;
  volatile i32* Abort;
  bool Runnable;
  bool HasWorker;
  Arena Worker;
  Array<Content> Result;
  bool IsError;
  bool Terminate;
};

struct AgentToolBatchPlan {
  Array<AgentPreparedTool> Calls;
  bool Sequential;
  u32 RunnableCount;
};

struct AgentToolCallResult {
  StrView ToolCallId;
  StrView ToolName;
  StrView EffectiveArgumentsJson;
  ContentArray Content;
  bool IsError;
  bool Terminate;
};

struct AgentToolBatchResult {
  Array<AgentToolCallResult> Calls;
  bool AnyTool;
  bool AllTerminate;
};

// Run's `out` borrows worker arenas from `plan`: commit results before
// AgentToolBatch_ReleaseWorkers.
void AgentToolBatch_Prepare(Arena* arena, const ToolRuntime* tools, u32 toolCount,
                            const Message* assistant, ToolExecution execution,
                            volatile i32* abort, const AgentHarnessHooks* hooks,
                            const AgentPermissionGate* permissions,
                            AgentToolBatchPlan* out);
void AgentToolBatch_Run(Arena* arena, AgentToolBatchPlan* plan,
                        u32 maxParallelTools, const AgentHarnessHooks* hooks,
                        const AgentToolDispatchGate* dispatch,
                        const AgentToolEventSink* events,
                        AgentToolBatchResult* out);
void AgentToolBatch_ReleaseWorkers(AgentToolBatchPlan* plan);

Message AgentToolResult_Materialize(const AgentToolCallResult* result);

bool AgentCheckpoint_ContinueTurn(bool anyTool, bool allToolsTerminate,
                                  u32 steeringCount);
bool AgentCheckpoint_ContinueRun(u32 followUpCount);
