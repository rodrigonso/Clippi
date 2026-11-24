#pragma once

#include "agent/harness.h"

SessionResult AgentDriver_Run(AgentHarness* harness, StrView prompt);

// `instructions` is never persisted; it applies to this compaction only.
SessionResult AgentDriver_Compact(AgentHarness* harness, StrView instructions);

// Folds history down to the keep-recent budget; `outStarted` is false when
// there was not more than that to fold, in which case nothing is published.
SessionResult AgentDriver_CompactIdle(AgentHarness* harness, bool* outStarted);

bool AgentDriver_ReviewPermission(
    void* user, Arena* arena, const AgentPermissionReviewRequest* request,
    volatile i32* abort, AgentPermissionReviewResult* out);

constexpr usize WORK_ARENA_KEEP_BYTES = (usize)1 << 20;

inline SessionResult AgentHook_Failure(SessionResult result, const char* fallback) {
  if (SessionResult_IsOk(result)) return result;
  return SessionResult_Error(
      SessionStatus::HookError,
      result.Message.Length ? result.Message : SV(fallback), result.Seq);
}

SessionResult AgentHarness_Append(AgentHarness* harness, SessionRecord* records,
                                  u32 count,
                                  bool rejectIfCancelled = false);

// The copy's backing arrays stay valid for the harness's lifetime: the arena
// never frees an abandoned block.
SessionReduction AgentHarness_ReduceSnapshot(AgentHarness* harness);

// Drains under StateLock. Queues are memory-only: a drained-but-unflushed item
// is lost if the process exits. `outMessages` get harness-lifetime clones.
SessionResult AgentHarness_FlushQueue(AgentHarness* harness, Array<Message>* queue,
                                      DurableId* ioHead, u32* outFlushedCount,
                                      Array<Message>* outMessages = nullptr);

void AgentHarness_PublishAgentStart(AgentHarness* harness, AgentTurnKind turn);
void AgentHarness_PublishAgentEnd(AgentHarness* harness, AgentTurnKind turn,
                                  AgentTurnOutcome outcome, StrView error);
void AgentHarness_PublishTurnStart(AgentHarness* harness, AgentTurnKind turn);
void AgentHarness_PublishTurnEnd(AgentHarness* harness, AgentTurnKind turn,
                                 AgentTurnOutcome outcome, StrView error);
void AgentHarness_PublishStream(AgentHarness* harness, const StreamEvent* event);
void AgentHarness_PublishToolStart(AgentHarness* harness, StrView toolCallId,
                                   StrView toolName, StrView argumentsJson);
void AgentHarness_PublishToolUpdate(AgentHarness* harness, StrView toolCallId,
                                    StrView toolName, const ToolResult* partial);
void AgentHarness_PublishToolEnd(AgentHarness* harness, StrView toolCallId,
                                 StrView toolName, const ToolResult* result,
                                 bool isError);
void AgentHarness_PublishRetry(AgentHarness* harness, AgentHarnessEventKind kind,
                               RetryLayer layer, u32 attempt, u32 maxAttempts,
                               u32 delayMs, StrView error);
void AgentHarness_PublishPermissionReviewStart(AgentHarness* harness,
                                               StrView toolName,
                                               StrView model);
void AgentHarness_PublishPermissionReviewEnd(
    AgentHarness* harness, StrView toolName, StrView model,
    AgentPermissionReviewOutcome outcome, StrView rationale);
void AgentHarness_PublishQueueUpdate(AgentHarness* harness);
