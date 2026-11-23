#include "tui/internal.h"

#include <string.h>

namespace TuiInternal {

static StrView HydrationText(Arena* scratch, const Message* message) {
  usize bytes = 0;
  u32 count = 0;
  StrView single = {};
  for (u32 i = 0; i < message->Content.Count; i++) {
    const Content& content = message->Content.Data[i];
    if (content.Kind != ContentKind::Text) continue;
    if (bytes) bytes++;
    bytes += content.Text.Length;
    single = content.Text;
    count++;
  }
  if (count < 2) return single;
  FFI(bytes >= 0xffffffffu);
  StrBuf text = StrBuf_Make(scratch, (u32)bytes + 1);
  for (u32 i = 0; i < message->Content.Count; i++) {
    const Content& content = message->Content.Data[i];
    if (content.Kind != ContentKind::Text) continue;
    if (text.Length) StrBuf_AppendChar(&text, '\n');
    StrBuf_Append(&text, content.Text);
  }
  return StrBuf_View(&text);
}

static void CloseHydratedRun(Tui* t, TuiHydratedRun* run) {
  if (run->Open && run->Responses) {
    TurnStats stats = run->Stats;
    stats.DurationMs =
        run->StartMs && run->LastMs > run->StartMs ? run->LastMs - run->StartMs : 0;
    LineModel_AddFooter(&t->Model, stats);
  }
  *run = {};
}

static void HydrateMessages(Tui* t, const Message* messages, u32 count) {
  TuiHydratedRun* run = &t->Hydrated;
  Arena scratch = ArenaCreate();
  for (u32 i = 0; i < count; i++) {
    ArenaReset(&scratch);
    ArenaTrim(&scratch, (usize)64 << 10);
    const Message* message = &messages[i];
    if (message->Kind == Role::User) {
      if (t->Model.HasAssistant) LineModel_EndAssistant(&t->Model);
      // A steer is logged after what the agent was doing when it was typed, so it is the
      // one message older than the message before it.
      bool steered = run->Open && message->Timestamp < run->LastMs;
      if (!steered) {
        CloseHydratedRun(t, run);
        run->Open = true;
        run->StartMs = message->Timestamp;
        run->LastMs = message->Timestamp;
      }
      LineModel_AddUser(&t->Model, HydrationText(&scratch, message), message->Timestamp,
                        steered);
    } else if (message->Kind == Role::Assistant) {
      if (!run->Open) {
        run->Open = true;
        run->StartMs = message->Timestamp;
      }
      if (message->Timestamp > run->LastMs) run->LastMs = message->Timestamp;
      run->Responses++;
      run->Stats.InputTokens += message->Use.Input;
      run->Stats.OutputTokens += message->Use.Output;
      bool hasText = false;
      for (u32 c = 0; c < message->Content.Count; c++)
        if (message->Content.Data[c].Kind == ContentKind::Text &&
            message->Content.Data[c].Text.Length)
          hasText = true;
      if (hasText) {
        LineModel_BeginAssistant(&t->Model);
        for (u32 c = 0; c < message->Content.Count; c++)
          if (message->Content.Data[c].Kind == ContentKind::Text)
            LineModel_AppendAssistant(&t->Model,
                                      message->Content.Data[c].Text);
        LineModel_EndAssistant(&t->Model);
      }
      for (u32 c = 0; c < message->Content.Count; c++) {
        const Content* content = &message->Content.Data[c];
        if (content->Kind == ContentKind::ToolCall) {
          LineModel_AddTool(&t->Model, content->ToolId, content->ToolName,
                            content->ToolArgsJson);
          run->Stats.Tools++;
        }
      }
      if (message->Stop == StopReason::Error)
        LineModel_AddNotice(&t->Model, EntryKind::Error,
                            message->ErrorMessage);
      else if (message->Stop == StopReason::Aborted)
        LineModel_AddNotice(&t->Model, EntryKind::Info,
                            SVL("Interrupted by user."));
    } else if (message->Kind == Role::ToolResult) {
      if (message->Timestamp > run->LastMs) run->LastMs = message->Timestamp;
      StrView result = HydrationText(&scratch, message);
      if (!LineModel_FinishTool(&t->Model, message->ToolCallId, result,
                                Truncate(result, 1500), message->IsError))
        LineModel_AddNotice(&t->Model, EntryKind::Error,
                            SVL("tool result did not match a pending call"));
    }
  }
  ArenaRelease(&scratch);
}

// Missing a SessionMem-owned field here is a use-after-free, not a leak.
void ClearSessionScopedStringsLocked(Tui* t) {
  t->Sess = TuiSessionStrings{};
  SetTransientText(&t->StateTool, {});
  SetTransientText(&t->RetryStatus, {});
  SetTransientText(&t->OperationLabel, {});
  SetTransientText(&t->OperationStatus, {});
}

// A modal or running operation shows SessionMem strings a waiting caller will read again.
bool SessionArenaSwapSafeLocked(const Tui* t) {
  return t->ModalKind == TuiModalKind::None &&
         t->ModalResult != TuiModalResult::Pending && !t->OperationActive;
}

// Only compaction may pass `allowSwap`: lag recovery runs on the event pump thread
// while the main thread holds unlocked StrViews into SessionMem (the submitted prompt).
void RebuildTranscriptLocked(Tui* t, const Message* messages, u32 count,
                                    bool allowSwap) {
  u16 width = t->Model.Width;
  u32 cap = t->Model.Cap;
  bool rounded = t->Model.Rounded;
  bool swap = allowSwap && SessionArenaSwapSafeLocked(t);

  Arena next = {};
  TuiSubmission preserved[64] = {};
  u32 preservedCount = 0;
  if (swap) {
    next = ArenaCreate();
    preservedCount = t->QCount;
    for (u32 i = 0; i < preservedCount; i++) {
      const TuiSubmission& queued = t->Queue[(t->QHead + i) % 64];
      preserved[i] = queued;
      preserved[i].Text = StrView_Dup(&next, queued.Text);
    }
  }

  LineModel_Release(&t->Model);
  if (swap) {
    Arena old = t->SessionMem;
    t->SessionMem = next;
    // Clone against the final arena address, not the stack-local arena handle:
    // every growable buffer must keep its Backing after this function returns.
    LineEdit_Clone(&t->Edit, &t->Edit, &t->SessionMem);
    ArenaRelease(&old);
    ClearSessionScopedStringsLocked(t);
  }
  LineModel_Init(&t->Model, &t->SessionMem, width, cap, t->Color, rounded, t->Accent);
  if (swap) {
    t->InputUi.Edit = &t->Edit;
    memset(t->Queue, 0, sizeof(t->Queue));
    for (u32 i = 0; i < preservedCount; i++) t->Queue[i] = preserved[i];
    t->QHead = 0;
    t->QTail = preservedCount % 64;
    t->QCount = preservedCount;
  }
  ResetDisplay(t);
  t->Use = {};
  SetTransientText(&t->StateTool, {});
  t->State = t->AgentRunning ? TuiState::Thinking : TuiState::Idle;
  t->ThinkingSinceMs = 0;
  t->Hydrated = {};
  HydrateMessages(t, messages, count);
  // A running agent's last run is still live: its totals carry on from the log,
  // since the display events that would have counted them were just dropped.
  if (!t->AgentRunning) {
    CloseHydratedRun(t, &t->Hydrated);
    ResetRunStats(t, 0);
  } else if (t->Hydrated.Open) {
    t->RunStartMs = t->Hydrated.StartMs;
    t->RunResponses = t->Hydrated.Responses;
    t->RunStats = t->Hydrated.Stats;
  } else {
    ResetRunStats(t, NowMillis());
  }
  t->Hydrated = {};
  LineModel_Layout(&t->Model);
  if (swap) t->SessionMemBaseline = t->SessionMem.Used;
}

}

using namespace TuiInternal;

void Tui_ReplaceTranscript(Tui* t, const Message* messages, u32 count, bool follow) {
  if (!t || (!messages && count)) return;
  Mutex_Lock(&t->Lock);
  RebuildTranscriptLocked(t, messages, count, false);
  if (follow) t->Follow = true;
  Wake(t);
  Mutex_Unlock(&t->Lock);
}

void Tui_AppendTranscript(Tui* t, const Message* messages, u32 count) {
  if (!t || !messages || !count) return;
  Mutex_Lock(&t->Lock);
  HydrateMessages(t, messages, count);
  LineModel_Layout(&t->Model);
  Wake(t);
  Mutex_Unlock(&t->Lock);
}

bool Tui_SessionMemoryWantsCompaction(Tui* t) {
  if (!t) return false;
  Mutex_Lock(&t->Lock);
  usize used = t->SessionMem.Used;
  bool worthIt = used > TUI_SESSION_COMPACT_FLOOR &&
                 used > t->SessionMemBaseline * 2;
  bool wants = t->SessionUp && worthIt && !t->AgentRunning && !t->Stop && !t->Quit &&
               !Tui_HasPendingDisplay(t) && SessionArenaSwapSafeLocked(t);
  Mutex_Unlock(&t->Lock);
  return wants;
}

bool Tui_CompactSessionMemory(Tui* t, const Message* messages, u32 count) {
  if (!t || (!messages && count)) return false;
  Mutex_Lock(&t->Lock);
  // Re-check under the lock: the caller sampled the condition before building
  // the transcript, and the input thread runs the whole time.
  if (!t->SessionUp || t->AgentRunning || t->Stop || t->Quit ||
      Tui_HasPendingDisplay(t) || !SessionArenaSwapSafeLocked(t)) {
    Mutex_Unlock(&t->Lock);
    return false;
  }
  RebuildTranscriptLocked(t, messages, count, true);
  t->Follow = true;
  Wake(t);
  Mutex_Unlock(&t->Lock);
  return true;
}