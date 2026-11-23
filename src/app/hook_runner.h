#pragma once
#include "app/hook_config.h"
#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"
#include "base/thread.h"

// Dispatch is queued to a worker so a slow hook never stalls the agent or TUI.

enum class HookEvent : u8 {
  SessionStart,
  UserPromptSubmit,
  PreToolUse,
  PostToolUse,
  Stop,
  StopFailure,
  SessionEnd,
};

StrView HookEvent_Name(HookEvent event);

// Fields over about 1 KiB are reported absent, not truncated.
struct HookSessionInfo {
  StrView SessionId;
  StrView WorkingDirectory;
  StrView TranscriptPath;
  // Clippi's answer to pi's `ctx.hasUI`.
  bool Interactive;
};

struct HookToolInfo {
  StrView Name;
  StrView CallId;
  StrView InputJson;
  bool InputOmitted;
};

struct HookRunner;

constexpr u32 HOOK_SESSION_ID_CAPACITY = 128;
constexpr u32 HOOK_PATH_CAPACITY = 1024;

// Inline so by-value snapshots survive concurrent switches without arena
// growth.
struct HookSessionStorage {
  char SessionId[HOOK_SESSION_ID_CAPACITY];
  u32 SessionIdLength;
  char WorkingDirectory[HOOK_PATH_CAPACITY];
  u32 WorkingDirectoryLength;
  char TranscriptPath[HOOK_PATH_CAPACITY];
  u32 TranscriptPathLength;
  bool Interactive;
};

void HookSession_Store(HookSessionStorage* out, const HookSessionInfo* source);

// Views borrow `storage`; keep it alive until the payload is built.
HookSessionInfo HookSession_View(const HookSessionStorage* storage);

// Null when no commands are registered. `config` must outlive the runner.
HookRunner* HookRunner_Create(Arena* arena, const HookConfig* config,
                              const HookSessionInfo* session);

// Safe to call while the worker is running.
void HookRunner_SetSession(HookRunner* runner, const HookSessionInfo* session);

// Never blocks; drops when full. Dispatch run-scoped events from the watch
// stream, or Stop can precede the run's last PostToolUse.
void HookRunner_Dispatch(HookRunner* runner, HookEvent event, StrView toolName);

// `finalEvent` runs inline after the join, immune to the queue's overflow drop.
void HookRunner_Shutdown(HookRunner* runner, HookEvent finalEvent);

void HookRunner_BuildPayload(StrBuf* out, HookEvent event, const HookSessionInfo* session,
                             const HookToolInfo* tool);
