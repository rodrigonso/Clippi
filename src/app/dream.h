#pragma once

#include "agent/harness.h"
#include "app/wiki.h"
#include "session/repository.h"

// Only a finished pass advances `.dream/state.json`; a failed one is retried.

constexpr u32 DREAM_INBOX_MAX_BYTES = 2u << 20;
constexpr u32 DREAM_MESSAGE_MAX_BYTES = 16u << 10;
constexpr u32 DREAM_TOOL_RESULT_MAX_BYTES = 1500;
constexpr u32 DREAM_TOOL_ARGS_MAX_BYTES = 400;
constexpr u32 DREAM_SNAPSHOTS_KEPT = 3;
constexpr u32 DREAM_LINT_FOLLOW_UPS = 2;
constexpr u64 DREAM_RECHECK_MS = 7ull * 24 * 60 * 60 * 1000;
constexpr u64 DREAM_FORGET_MS = 90ull * 24 * 60 * 60 * 1000;

StrView Dream_ExpandPrompt(Arena* arena, StrView prompt, const WikiLocation* location);

struct DreamDigest {
  StrView Markdown;
  u64 FirstSeq;
  u64 LastSeq;
  bool Truncated;
};

// Reasoning is never included.
void Dream_RenderDigest(Arena* arena, const SessionRecord* records, u32 count,
                        u64 afterSeq, StrView sessionId, StrView workingDirectory,
                        u32 maxBytes, DreamDigest* out);

struct DreamSessionState {
  StrView SessionId;
  StrView ProjectKey;
  StrView WorkingDirectory;
  u64 Seq;
  u64 SeenMs;
};

struct DreamState {
  u64 JournalOffset;
  Array<DreamSessionState> Sessions;
};

// An unreadable file is an error so a pass never silently starts over.
bool DreamState_Load(Arena* arena, StrView path, DreamState* out);
bool DreamState_Save(Arena* arena, StrView path, const DreamState* state);

struct DreamRun {
  WikiLocation Location;
  SessionRepository* Sources;
  SessionRepository* Sessions;
  Provider ProviderImpl;
  Model ModelInfo;
  ModelRequestProfile Profile;
  void* AuthUser;
  AgentResolveAuthFn ResolveAuth;
  AgentPermissionReviewer Reviewer;
  StrView ReviewerModel;
  f64 ReviewerThreshold;
  AgentRetryPolicy Retry;
  AgentTurnLimitPolicy TurnLimit;
  AgentCompactionPolicy Compaction;
  StrView SystemPrompt;
  AgentHarnessEventObserver Events;
  u64 NowMs;
};

enum class DreamOutcome : u8 { Dreamed, NothingToDo, Busy, Failed };

struct DreamResult {
  DreamOutcome Outcome;
  u32 Digests;
  u32 PagesChanged;
  DurableId SessionId;
  StrView Message;
};

// Temporarily changes the process working directory. Results owned by `arena`.
void Dream_Execute(Arena* arena, const DreamRun* run, DreamResult* out);

// Only the dream lock holder writes progress.json, atomically, before
// unlocking.

enum class DreamPhase : u8 { Preparing, Dreaming, Finished };

struct DreamProgress {
  u64 StartedMs;
  DreamPhase Phase;
  StrView SessionId;
  StrView ProjectKey;
  DreamOutcome Outcome;
  u32 Digests;
  u32 PagesChanged;
  StrView Message;
};

bool DreamProgress_Load(Arena* arena, StrView path, DreamProgress* out);
bool DreamProgress_Save(Arena* arena, StrView path, const DreamProgress* progress);

enum class DreamWatchState : u8 {
  Starting,
  Preparing,
  Dreaming,
  Finished,
  Stopped,
  NotStarted,
};

constexpr u64 DREAM_WATCH_START_TIMEOUT_MS = 20000;

// Progress older than `sinceMs` is an earlier pass's; 0 follows the lock
// holder.
DreamWatchState Dream_WatchState(bool running, const DreamProgress* progress,
                                 u64 sinceMs, u64 nowMs);

// Lock first, then progress, so a pass seen without the lock is seen finished.
bool Dream_ReadProgress(Arena* arena, const WikiLocation* location, bool* running,
                        DreamProgress* out);

SessionResult Dream_ReadMessages(Arena* arena, SessionRepository* sessions,
                                 const DreamProgress* progress, Array<Message>* out);

struct DreamCommandLine {
  StrView WorkingDirectory;
  StrView Store;
  StrView DataRoot;
  StrView AgentDirectory;
  StrView SettingsPath;
  StrView AuthPath;
  StrView TrustPath;
  const char* ModelOverride;
};

int Dream_RunCommandLine(Arena* arena, const DreamCommandLine* options);

bool Dream_SpawnBackground(Arena* arena, StrView workingDirectory);

enum class DreamRestoreResult : u8 { Restored, NoSnapshot, Busy, Failed };

DreamRestoreResult Dream_RestoreLatestSnapshot(Arena* arena,
                                               const WikiLocation* location,
                                               u32* restored, StrView* snapshot);

struct DreamStatus {
  bool Running;
  bool HasLastDream;
  WikiJournalEntry LastDream;
  u32 PendingSessions;
  u32 Snapshots;
};

void Dream_ReadStatus(Arena* arena, const WikiLocation* location, DreamStatus* out);
