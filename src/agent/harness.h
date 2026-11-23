#pragma once

#include "agent/ask.h"
#include "agent/compaction.h"
#include "agent/harness_event.h"
#include "agent/hooks.h"
#include "agent/permissions.h"
#include "agent/primitives.h"
#include "session/reducer.h"
#include "session/repository.h"

struct AgentHarnessConfig {
  StrView SystemPrompt;
  Model ModelInfo;
  const Tool* Tools;
  u32 ToolCount;
  Provider ProviderImpl;
  StreamOptions Stream;
  ToolExecution ToolExec;
  u32 MaxParallelTools;
  AgentRetryPolicy Retry;
  // Zero-initialized means unlimited (pi's behaviour).
  AgentTurnLimitPolicy TurnLimit;
  AgentCompactionPolicy Compaction;
  AgentHarnessHooks Hooks;
  AgentHarnessEventObserver Events;
  AgentAskUser AskUser;
  // Yolo default keeps zero-initialized embedding configs running every tool.
  AgentPermissionMode PermissionMode = AgentPermissionMode::Yolo;
  StrView PermissionReviewerModel;
  f64 PermissionReviewerThreshold = AGENT_PERMISSION_DEFAULT_REVIEWER_THRESHOLD;
  AgentPermissionReviewer PermissionReviewer;
  StrView WorkingDirectory;
  const StrView* WritableRoots;
  u32 WritableRootCount;
  void* AuthUser;
  AgentResolveAuthFn ResolveAuth;
};

// Memory-only (pi semantics): lost if the process exits before a checkpoint.
struct AgentHarnessQueues {
  Array<Message> Steering;
  Array<Message> FollowUp;
  Array<Message> NextTurn;
};

struct AgentHarness {
  SessionLog Log;
  AgentHarnessConfig Config;
  Arena Runtime;
  Arena State;    // append-only
  Arena Queued;   // recycled whenever all queues drain
  // Reset every turn: nothing allocated here may outlive its turn.
  Arena Work;
  // Reset with Work at the end of each Run/Compact/Navigate.
  Arena RunMem;
  // Not State: State is append-only and would leak a copy per failure.
  Arena LastResult;
  ToolRuntime* RuntimeTools;
  Mutex StateLock;  // guards Records/Reduction/Queues/Running/Faulted/Watchers
  // Guards PublishedCursor and watchers. Lock order is
  // StateLock -> WatcherLock -> AgentHarnessWatch::Lock, never the reverse.
  Mutex WatcherLock;

  // Loaded once at Init, then folded per append; never reloaded.
  Array<SessionRecord> Records;
  SessionReduction Reduction;
  AgentHarnessQueues Queues;
  AgentPermissionEngine Permissions;

  // Access only via Atomic_StoreI32 / Atomic_LoadI32.
  volatile i32 AbortFlag;
  bool Running;
  bool Faulted;

  AgentHarnessWatch* Watchers[AGENT_HARNESS_MAX_WATCHERS];
  u32 WatcherCount;
  u64 PublishedCursor;
  bool Initialized;
};

// Takes ownership of log.
SessionResult AgentHarness_Init(AgentHarness* harness, SessionLog log,
                                const AgentHarnessConfig* config);
SessionResult AgentHarness_Close(AgentHarness* harness);

SessionResult AgentHarness_Snapshot(Arena* arena, AgentHarness* harness,
                                    SessionReduction* out);

struct AgentHarnessSummary {
  DurableId SessionId;
  DurableId HeadId;
  StrView Provider;
  StrView Model;
  u32 RecordCount;
  u64 Cursor;
  bool Running;
  bool Faulted;
};
SessionResult AgentHarness_Summarize(Arena* arena, AgentHarness* harness,
                                     AgentHarnessSummary* out);
SessionResult AgentHarness_DescribeSession(Arena* arena, AgentHarness* harness,
                                           SessionInfo* out);
// Returned content is caller-owned and survives harness close.
SessionResult AgentHarness_SnapshotTranscript(Arena* arena, AgentHarness* harness,
                                              u32 maxMessages,
                                              AgentTranscriptSnapshot* out);
SessionResult AgentHarness_Watch(AgentHarness* harness, u32 capacity,
                                 AgentHarnessWatch* outWatch);
SessionResult AgentHarness_WatchTranscript(Arena* arena, AgentHarness* harness,
                                           u32 capacity, u32 maxMessages,
                                           AgentTranscriptSnapshot* outSnapshot,
                                           AgentHarnessWatch* outWatch);
SessionResult AgentHarness_WatchNext(Arena* arena, AgentHarnessWatch* watch,
                                     AgentHarnessEvent* out);
SessionResult AgentHarness_WatchRequestClose(AgentHarnessWatch* watch);
SessionResult AgentHarness_WatchClose(AgentHarnessWatch* watch);
// A result's Message stays valid only until the next accepted run, compaction,
// navigation, metadata change, fork, or Close.
SessionResult AgentHarness_Run(AgentHarness* harness, StrView prompt);
SessionResult AgentHarness_Compact(AgentHarness* harness,
                                   StrView instructions = StrView{});
SessionResult AgentHarness_Navigate(AgentHarness* harness, DurableId targetEntryId);
SessionResult AgentHarness_LabelBranch(AgentHarness* harness, StrView label);
SessionResult AgentHarness_SetName(AgentHarness* harness, StrView name);
SessionResult AgentHarness_SetModel(AgentHarness* harness, Model model,
                                    Provider provider,
                                    ModelRequestProfile profile);
SessionResult AgentHarness_Fork(Arena* arena, AgentHarness* harness,
                                SessionRepository* repository,
                                DurableId sourceHeadId,
                                DurableId requestedSessionId, StrView label,
                                SessionLog* outLog, SessionInfo* outInfo);
SessionResult AgentHarness_RequestCancel(AgentHarness* harness);

AgentPermissionMode AgentHarness_GetPermissionMode(AgentHarness* harness);
AgentPermissionMode AgentHarness_SetPermissionMode(AgentHarness* harness,
                                                   AgentPermissionMode mode);
AgentPermissionMode AgentHarness_CyclePermissionMode(AgentHarness* harness);
AgentPermissionMode AgentHarness_ResetPermissions(AgentHarness* harness);
u32 AgentHarness_PermissionGrantCount(AgentHarness* harness);
// `roots` must stay valid while this harness or a copy of its config is in use.
void AgentHarness_SetWritableRoots(AgentHarness* harness, const StrView* roots,
                                   u32 count);
bool AgentHarness_ResolvePermissionRequest(AgentHarness* harness, Arena* arena,
                                           StrView requestId,
                                           volatile i32* abort,
                                           StrView* outMessage, StrBuf* error);

SessionResult AgentHarness_QueueSteering(AgentHarness* harness, StrView text);
SessionResult AgentHarness_QueueFollowUp(AgentHarness* harness, StrView text);
SessionResult AgentHarness_QueueNextTurn(AgentHarness* harness, StrView text);
