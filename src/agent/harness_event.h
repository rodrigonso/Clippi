#pragma once

#include "agent/hooks.h"
#include "agent/tool.h"
#include "base/thread.h"
#include "session/reducer.h"

struct AgentHarness;

constexpr u32 AGENT_HARNESS_MAX_WATCHERS = 8;
constexpr u32 AGENT_HARNESS_MAX_WATCH_EVENTS = 256;

enum class RetryLayer : u8 { ProviderRequest, AssistantTurn };

enum class AgentPermissionReviewOutcome : u8 {
  Allow,
  Ask,
  Unavailable,
};

// Transient events are never persisted; only durable ones are authoritative.
enum class AgentHarnessEventKind : u8 {
  DurableRecord,
  TransientAgentStart,
  TransientAgentEnd,
  TransientTurnStart,
  TransientTurnEnd,
  TransientStream,
  TransientToolStart,
  TransientToolUpdate,
  TransientToolEnd,
  TransientRetryStart,
  TransientRetryEnd,
  TransientPermissionReviewStart,
  TransientPermissionReviewEnd,
  TransientQueueUpdate,
  TransientCancelRequested,
  TransientEventsDropped,
};

struct AgentHarnessTurnEvent {
  AgentTurnKind Kind;
  AgentTurnOutcome Outcome;
  StrView Error;
};

struct AgentHarnessToolEvent {
  StrView CallId;
  StrView Name;
  StrView ArgumentsJson;
  ToolResult Result;
  bool IsError;
};

struct AgentHarnessRetryEvent {
  RetryLayer Layer;
  u32 Attempt;
  u32 MaxAttempts;
  u32 DelayMs;
  StrView Error;
};

struct AgentHarnessQueueEvent {
  u32 SteerCount;
  u32 FollowUpCount;
  u32 NextTurnCount;
};

struct AgentHarnessPermissionReviewEvent {
  StrView ToolName;
  StrView Model;
  AgentPermissionReviewOutcome Outcome;
  StrView Rationale;
};

// Only transient events are ever dropped; durable records never are.
struct AgentHarnessDroppedEvent {
  u32 Count;
};

// Members must stay trivially copyable. `Record` is a pointer to keep events
// small; it is owned by whichever arena cloned the event.
struct AgentHarnessEvent {
  AgentHarnessEventKind Kind;
  u64 Cursor;
  union {
    const SessionRecord* Record;
    StreamEvent Stream;
    AgentHarnessTurnEvent Turn;
    AgentHarnessToolEvent Tool;
    AgentHarnessRetryEvent Retry;
    AgentHarnessPermissionReviewEvent PermissionReview;
    AgentHarnessQueueEvent Queue;
    AgentHarnessDroppedEvent Dropped;
  };
};

// Payloads are borrowed and valid only during OnEvent. Called under
// WatcherLock (sometimes StateLock): must not reenter or wait on the harness.
struct AgentHarnessEventObserver {
  void* User;
  void (*OnEvent)(void* user, const AgentHarnessEvent* event);
};

// Messages are deep copies; a snapshot shares nothing with the harness.
struct AgentTranscriptSnapshot {
  Array<Message> Transcript;
  DurableId HeadId;
  u64 Cursor;
  u32 ContextTokens;
  bool Running;
  bool Faulted;
  bool HasEarlierMessages;
};

// One arena per watcher, not per slot: per-slot arenas would commit 16 MiB.
struct AgentHarnessWatchSlot {
  AgentHarnessEvent Event;
  // Lets coalesced deltas append in place; without it merging is O(length^2).
  u32 StreamCapacity;
};

struct AgentHarnessWatch {
  AgentHarness* Owner;
  DurableId Id;
  Mutex Lock;
  CondVar Changed;
  Arena Mem;
  AgentHarnessWatchSlot Slots[AGENT_HARNESS_MAX_WATCH_EVENTS];
  u32 Capacity;
  u32 Head;
  u32 Count;
  u64 Cursor;
  u32 DroppedTransient;
  bool Initialized;
  bool Registered;
  bool Lagged;
  bool Closed;
  u32 Waiting;
};
