#include "app/hook_runner.h"

#include "base/thread.h"
#include "json/json.h"
#include "tools/exec.h"

namespace {

constexpr u32 HOOK_QUEUE_CAPACITY = 64;
constexpr u32 HOOK_TOOL_NAME_CAPACITY = 64;
constexpr u32 HOOK_SHUTDOWN_DRAIN_MS = 5000;

struct HookQueueItem {
  HookEvent Event;
  char ToolName[HOOK_TOOL_NAME_CAPACITY];
  u32 ToolNameLength;
};

// Too long is absent, not truncated: a consumer would act on half a path.
void StoreField(char* destination, u32 capacity, u32* outLength, StrView value) {
  if (value.Length > capacity) {
    *outLength = 0;
    return;
  }
  if (value.Length) memcpy(destination, value.Data, value.Length);
  *outLength = value.Length;
}

}

void HookSession_Store(HookSessionStorage* out, const HookSessionInfo* source) {
  memset(out, 0, sizeof(*out));
  if (!source) return;
  StoreField(out->SessionId, HOOK_SESSION_ID_CAPACITY, &out->SessionIdLength,
             source->SessionId);
  StoreField(out->WorkingDirectory, HOOK_PATH_CAPACITY, &out->WorkingDirectoryLength,
             source->WorkingDirectory);
  StoreField(out->TranscriptPath, HOOK_PATH_CAPACITY, &out->TranscriptPathLength,
             source->TranscriptPath);
  out->Interactive = source->Interactive;
}

HookSessionInfo HookSession_View(const HookSessionStorage* storage) {
  HookSessionInfo out = {};
  out.SessionId = StrView{storage->SessionId, storage->SessionIdLength};
  out.WorkingDirectory = StrView{storage->WorkingDirectory, storage->WorkingDirectoryLength};
  out.TranscriptPath = StrView{storage->TranscriptPath, storage->TranscriptPathLength};
  out.Interactive = storage->Interactive;
  return out;
}

struct HookRunner {
  const HookConfig* Config;
  HookSessionStorage Session;

  Mutex Lock;
  CondVar Changed;
  HookQueueItem Items[HOOK_QUEUE_CAPACITY];
  u32 Head;
  u32 Count;
  bool StopRequested;
  bool Abandon;
  bool Busy;
  // Separate from StopRequested so the terminal event still runs after stop.
  volatile i32 Abort;

  Thread Worker;
  bool Started;
};

StrView HookEvent_Name(HookEvent event) {
  switch (event) {
    case HookEvent::SessionStart: return SVL("SessionStart");
    case HookEvent::UserPromptSubmit: return SVL("UserPromptSubmit");
    case HookEvent::PreToolUse: return SVL("PreToolUse");
    case HookEvent::PostToolUse: return SVL("PostToolUse");
    case HookEvent::Stop: return SVL("Stop");
    case HookEvent::StopFailure: return SVL("StopFailure");
    case HookEvent::SessionEnd: return SVL("SessionEnd");
  }
  return StrView{};
}

void HookRunner_BuildPayload(StrBuf* out, HookEvent event, const HookSessionInfo* session,
                             const HookToolInfo* tool) {
  JsonWriter w;
  JsonWrite_Begin(&w, out);
  JsonWrite_String(&w, "hook_event_name", HookEvent_Name(event));
  if (session) {
    if (session->SessionId.Length) JsonWrite_String(&w, "session_id", session->SessionId);
    if (session->WorkingDirectory.Length)
      JsonWrite_String(&w, "cwd", session->WorkingDirectory);
    if (session->TranscriptPath.Length)
      JsonWrite_String(&w, "transcript_path", session->TranscriptPath);
  }
  if (tool && tool->Name.Length) {
    JsonWrite_String(&w, "tool_name", tool->Name);
    if (tool->CallId.Length) JsonWrite_String(&w, "tool_call_id", tool->CallId);
    if (tool->InputJson.Length) {
      JsonWrite_Key(&w, "tool_input");
      StrBuf_Append(out, tool->InputJson);
    }
    if (tool->InputOmitted) JsonWrite_Bool(&w, "tool_input_omitted", true);
  }
  if (session && event == HookEvent::PreToolUse)
    JsonWrite_Bool(&w, "interactive", session->Interactive);
  JsonWrite_End(&w);
}

namespace {

// Caller-owned snapshot: HookRunner_SetSession may rewrite runner->Session.
void RunOne(HookRunner* runner, Arena* scratch, const HookQueueItem* item,
            const HookSessionInfo* session, volatile i32* abort) {
  StrView toolName = {item->ToolName, item->ToolNameLength};
  u32 capacity = runner->Config->CommandCount;
  if (!capacity) return;
  const HookCommand** commands = ArenaPushArray(scratch, const HookCommand*, capacity);
  u32 count = HookConfig_Select(runner->Config, HookEvent_Name(item->Event), toolName,
                                commands, capacity);
  if (!count) return;

  StrBuf payload = StrBuf_Make(scratch, 512);
  HookToolInfo toolInfo = {};
  toolInfo.Name = toolName;
  HookRunner_BuildPayload(&payload, item->Event, session, &toolInfo);
  StrView payloadView = StrBuf_View(&payload);

  // Output and exit code are ignored: a broken hook is not an agent failure.
  for (u32 i = 0; i < count; i++) {
    // Also checked between commands, so an abandoned shutdown stops spawning.
    if (abort && Atomic_LoadI32(abort)) break;
    Exec_RunShellInput(scratch, commands[i]->Command, payloadView, commands[i]->TimeoutMs,
                       abort);
  }
}

void RunHookWorker(void* user) {
  HookRunner* runner = (HookRunner*)user;
  Arena scratch = ArenaCreate((usize)1 << 20);
  for (;;) {
    Mutex_Lock(&runner->Lock);
    while (!runner->Count && !runner->StopRequested) Cond_Wait(&runner->Changed, &runner->Lock);
    if (!runner->Count || runner->Abandon) {
      Mutex_Unlock(&runner->Lock);
      break;
    }
    HookQueueItem item = runner->Items[runner->Head];
    runner->Head = (runner->Head + 1) % HOOK_QUEUE_CAPACITY;
    runner->Count--;
    // By value: HookRunner_SetSession may rewrite the copy after unlock.
    HookSessionStorage session = runner->Session;
    runner->Busy = true;
    Mutex_Unlock(&runner->Lock);

    ArenaReset(&scratch);
    HookSessionInfo info = HookSession_View(&session);
    RunOne(runner, &scratch, &item, &info, &runner->Abort);

    Mutex_Lock(&runner->Lock);
    runner->Busy = false;
    Cond_Broadcast(&runner->Changed);
    Mutex_Unlock(&runner->Lock);
  }
  ArenaRelease(&scratch);
}

}

HookRunner* HookRunner_Create(Arena* arena, const HookConfig* config,
                              const HookSessionInfo* session) {
  if (!arena || !config || !config->CommandCount) return nullptr;

  HookRunner* runner = ArenaPushStruct(arena, HookRunner);
  // memset: the volatile Abort makes copy-assignment ill-formed and crashes
  // MSVC.
  memset(runner, 0, sizeof(*runner));
  runner->Config = config;
  HookSession_Store(&runner->Session, session);
  Mutex_Init(&runner->Lock);
  Cond_Init(&runner->Changed);

  if (!Thread_Start(&runner->Worker, RunHookWorker, runner)) {
    Cond_Destroy(&runner->Changed);
    Mutex_Destroy(&runner->Lock);
    return nullptr;
  }
  runner->Started = true;
  return runner;
}

void HookRunner_SetSession(HookRunner* runner, const HookSessionInfo* session) {
  if (!runner || !session) return;

  // Copied under the lock so the worker never observes a half-updated identity.
  Mutex_Lock(&runner->Lock);
  HookSession_Store(&runner->Session, session);
  Mutex_Unlock(&runner->Lock);
}

void HookRunner_Dispatch(HookRunner* runner, HookEvent event, StrView toolName) {
  if (!runner || !runner->Started) return;

  HookQueueItem item = {};
  item.Event = event;
  item.ToolNameLength = toolName.Length < HOOK_TOOL_NAME_CAPACITY ? toolName.Length
                                                                 : HOOK_TOOL_NAME_CAPACITY - 1;
  if (item.ToolNameLength) memcpy(item.ToolName, toolName.Data, item.ToolNameLength);
  item.ToolName[item.ToolNameLength] = '\0';

  Mutex_Lock(&runner->Lock);
  if (!runner->StopRequested && runner->Count < HOOK_QUEUE_CAPACITY) {
    runner->Items[(runner->Head + runner->Count) % HOOK_QUEUE_CAPACITY] = item;
    runner->Count++;
    Cond_Signal(&runner->Changed);
  }
  Mutex_Unlock(&runner->Lock);
}

void HookRunner_Shutdown(HookRunner* runner, HookEvent finalEvent) {
  if (!runner || !runner->Started) return;

  Mutex_Lock(&runner->Lock);
  runner->StopRequested = true;
  Cond_Broadcast(&runner->Changed);
  Mutex_Unlock(&runner->Lock);

  u64 deadline = MonotonicMillis() + HOOK_SHUTDOWN_DRAIN_MS;
  for (;;) {
    Mutex_Lock(&runner->Lock);
    bool idle = !runner->Count && !runner->Busy;
    if (!idle && MonotonicMillis() >= deadline) {
      runner->Abandon = true;
      runner->Abort = 1;
      idle = true;
    }
    Mutex_Unlock(&runner->Lock);
    if (idle) break;
    Sleep_Ms(5);
  }

  Thread_Join(&runner->Worker);
  runner->Started = false;

  // Caller's thread, uncancelled: the one report consumers cannot do without.
  runner->Abort = 0;
  HookQueueItem terminal = {};
  terminal.Event = finalEvent;
  HookSessionInfo info = HookSession_View(&runner->Session);
  Arena scratch = ArenaCreate((usize)1 << 20);
  RunOne(runner, &scratch, &terminal, &info, nullptr);
  ArenaRelease(&scratch);

  Cond_Destroy(&runner->Changed);
  Mutex_Destroy(&runner->Lock);
}
