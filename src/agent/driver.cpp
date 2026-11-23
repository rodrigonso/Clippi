#include "agent/driver.h"

#include "agent/compaction.h"
#include "base/thread.h"
#include "json/json.h"
#include "llm/retry.h"

namespace {

bool WorkOwns(const Arena* arena, const void* data, usize bytes) {
  usize offset = (usize)data - (usize)arena->Base;
  return arena->Base && offset <= arena->Used && bytes <= arena->Used - offset;
}

StrView MutableText(Arena* arena, StrView text) {
  return !text.Length || WorkOwns(arena, text.Data, text.Length)
             ? text : StrView_Dup(arena, text);
}

template <typename T>
void OwnMutableArray(Arena* arena, Array<T>* array) {
  if (array->Backing == arena &&
      WorkOwns(arena, array->Data, (usize)array->Capacity * sizeof(T))) return;
  Array<T> owned = Array_Make<T>(arena, array->Count);
  for (u32 i = 0; i < array->Count; i++) Array_Push(&owned, array->Data[i]);
  *array = owned;
}

void OwnMutableMessage(Arena* arena, Message* message) {
  OwnMutableArray(arena, &message->Content);
  for (u32 i = 0; i < message->Content.Count; i++) {
    Content* content = &message->Content.Data[i];
    content->Text = MutableText(arena, content->Text);
    content->ToolId = MutableText(arena, content->ToolId);
    content->ToolName = MutableText(arena, content->ToolName);
    content->ToolArgsJson = MutableText(arena, content->ToolArgsJson);
    content->ImageData = MutableText(arena, content->ImageData);
    content->ImageMimeType = MutableText(arena, content->ImageMimeType);
    content->Signature = MutableText(arena, content->Signature);
  }
  message->ErrorMessage = MutableText(arena, message->ErrorMessage);
  message->Model = MutableText(arena, message->Model);
  message->Provider = MutableText(arena, message->Provider);
  message->ToolCallId = MutableText(arena, message->ToolCallId);
  message->ToolName = MutableText(arena, message->ToolName);
}

// Hooks may swap owned fields for borrowed slices: recheck at each mutable
// boundary, but keep already detached Work storage across retries.
void OwnMutableMessages(Arena* arena, Array<Message>* messages) {
  OwnMutableArray(arena, messages);
  for (u32 i = 0; i < messages->Count; i++)
    OwnMutableMessage(arena, &messages->Data[i]);
}

void OwnMutableProviderInput(Arena* arena, AgentHarnessProviderInput* input) {
  Model* model = &input->ModelInfo;
  model->Id = MutableText(arena, model->Id);
  model->Name = MutableText(arena, model->Name);
  model->Provider = MutableText(arena, model->Provider);
  model->BaseUrl = MutableText(arena, model->BaseUrl);
  LlmContext* context = &input->Context;
  context->SystemPrompt = MutableText(arena, context->SystemPrompt);
  Array<Message> messages = {(Message*)context->Messages, context->MessageCount,
                             context->MessageCount, arena};
  OwnMutableMessages(arena, &messages);
  context->Messages = messages.Data;
  Array<ToolDef> tools = {(ToolDef*)context->Tools, context->ToolCount,
                          context->ToolCount, arena};
  OwnMutableArray(arena, &tools);
  for (u32 i = 0; i < tools.Count; i++) {
    ToolDef* tool = &tools.Data[i];
    tool->Name = MutableText(arena, tool->Name);
    tool->Description = MutableText(arena, tool->Description);
    tool->ParamsJsonSchema = MutableText(arena, tool->ParamsJsonSchema);
  }
  context->Tools = tools.Data;
  input->Options.Auth.Token = MutableText(arena, input->Options.Auth.Token);
  input->Options.Auth.AccountId = MutableText(arena, input->Options.Auth.AccountId);
  input->Options.Request.ToolName = MutableText(arena, input->Options.Request.ToolName);
}

void OwnMutableTurnContext(Arena* arena, AgentTurnContext* context) {
  if (context->Assistant) {
    Message* assistant = ArenaPushStruct(arena, Message);
    *assistant = *context->Assistant;
    OwnMutableMessage(arena, assistant);
    context->Assistant = assistant;
  }
  OwnMutableMessages(arena, &context->ToolResults);
  OwnMutableMessages(arena, &context->Context);
  OwnMutableMessages(arena, &context->NewMessages);
}

SessionRecord NewMessageRecord(DurableId parentId, const Message& value) {
  SessionRecord record = {};
  record.EntryId = DurableId_Generate();
  record.Kind = SessionRecordKind::Message;
  record.MessageEntry.ParentId = parentId;
  record.MessageEntry.Value = value;
  return record;
}

bool ResolveProviderInput(AgentHarnessProviderInput* input) {
  if (!input || input->ModelInfo.Provider.Length == 0 || input->ModelInfo.Id.Length == 0 ||
      input->Options.MaxTokens < 0 ||
      !Model_RequestProfileIsValid(&input->ModelInfo, input->Options.Profile))
    return false;
  input->Options.Profile =
      Model_ResolveRequestProfile(&input->ModelInfo, input->Options.Profile);
  return true;
}

void OnProviderStreamEvent(void* user, const StreamEvent& event) {
  AgentHarness* harness = (AgentHarness*)user;
  AgentHarness_PublishStream(harness, &event);
}

void OnProviderRequestRetry(void* user, const HttpRetryEvent* retry) {
  AgentHarness* harness = (AgentHarness*)user;
  AgentHarnessEventKind kind = retry->Kind == HttpRetryEventKind::Start
                                   ? AgentHarnessEventKind::TransientRetryStart
                                   : AgentHarnessEventKind::TransientRetryEnd;
  StrView error = retry->Error.Length ? retry->Error : SVL("retryable HTTP response");
  AgentHarness_PublishRetry(harness, kind, RetryLayer::ProviderRequest,
                            retry->Attempt, retry->MaxRetries, retry->DelayMs,
                            error);
}

void OnToolUpdateBridge(void* user, StrView toolCallId, StrView toolName,
                        const ToolResult* partial) {
  AgentHarness_PublishToolUpdate((AgentHarness*)user, toolCallId, toolName, partial);
}

void OnToolEndBridge(void* user, StrView toolCallId, StrView toolName,
                     const ToolResult* result, bool isError) {
  AgentHarness_PublishToolEnd((AgentHarness*)user, toolCallId, toolName, result, isError);
}

AgentToolEventSink ToolEvents(AgentHarness* harness) {
  AgentToolEventSink sink = {};
  sink.User = harness;
  sink.OnUpdate = OnToolUpdateBridge;
  sink.OnEnd = OnToolEndBridge;
  return sink;
}

bool TryStartTool(void* user) {
  AgentHarness* harness = (AgentHarness*)user;
  ScopedLock lock(&harness->StateLock);
  return !Atomic_LoadI32(&harness->AbortFlag);
}

struct TurnRunResult {
  Message Assistant;
  bool ProviderOk;
};

// Memory-only; overrides expire when the run returns. Only NewMessages uses
// RunMem so override owners can be reclaimed without discarding it.
struct AgentRunState {
  Array<Message> NewMessages;
  bool HasContext;
  Array<Message> Context;
  // Stable arena addresses keep nested Array/StrBuf backing pointers valid.
  // Stage into the other slot before releasing anything the update may borrow.
  Arena ContextMem[2];
  u32 ContextSlot;
  bool HasModel;
  Model ModelInfo;
  Arena ModelMem;
  bool HasProfile;
  ModelRequestProfile Profile;
  // Outside Work because ContinueRun resets Work every iteration.
  u32 AutonomousTurns;
  // Turns covered by user grants, kept apart so AutonomousTurns stays the true count.
  u32 GrantedTurns;
};

void AgentRunState_RecordMessages(AgentRunState* run, const Message* messages, u32 count) {
  if (!run) return;
  for (u32 i = 0; i < count; i++) {
    Array_Push(&run->NewMessages, messages[i]);
    // Must accumulate like pi's currentContext, or a frozen snapshot is re-sent forever.
    if (run->HasContext) Array_Push(&run->Context, messages[i]);
  }
}

void AgentRunState_Record(AgentRunState* run, const SessionRecord* records, u32 count) {
  if (!run) return;
  for (u32 i = 0; i < count; i++) {
    if (records[i].Kind != SessionRecordKind::Message) continue;
    AgentRunState_RecordMessages(run, &records[i].MessageEntry.Value, 1);
  }
}

void AgentRunState_DropContext(AgentRunState* run) {
  run->HasContext = false;
  run->Context = {};
  ArenaRelease(&run->ContextMem[run->ContextSlot]);
}

bool AgentRunState_OwnsText(const AgentRunState* run, StrView text) {
  const Arena* owners[] = {&run->ContextMem[run->ContextSlot], &run->ModelMem};
  for (const Arena* owner : owners) {
    usize offset = (usize)text.Data - (usize)owner->Base;
    if (owner->Base && offset < owner->Used) return true;
  }
  return false;
}

SessionResult AgentRunState_Apply(AgentRunState* run,
                                  const AgentNextTurnUpdate* update) {
  if (!run || !update) return SessionResult_Ok();
  if (update->HasModel &&
      (update->ModelInfo.Provider.Length == 0 || update->ModelInfo.Id.Length == 0))
    return SessionResult_Error(SessionStatus::HookError,
                               SVL("prepare-next-turn hook produced an invalid model"));

  Arena modelMem = {};
  Model model = {};
  Array<Message> context = {};
  u32 contextSlot = run->ContextSlot ^ 1;
  if (update->HasModel) {
    modelMem = ArenaCreate();
    model = update->ModelInfo;
    model.Id = StrView_Dup(&modelMem, model.Id);
    model.Name = StrView_Dup(&modelMem, model.Name);
    model.Provider = StrView_Dup(&modelMem, model.Provider);
    model.BaseUrl = StrView_Dup(&modelMem, model.BaseUrl);
  }
  if (update->HasContext) {
    Arena* mem = &run->ContextMem[contextSlot];
    *mem = ArenaCreate();
    context = Array_Make<Message>(mem, update->Context.Count + 8);
    for (u32 i = 0; i < update->Context.Count; i++)
      Array_Push(&context, Message_Clone(mem, update->Context.Data[i]));
  }

  // Both fields may borrow either old owner or Work. Finish all copies before
  // releasing either owner, including when context and model borrow each other.
  if (update->HasModel) {
    ArenaRelease(&run->ModelMem);
    // Model contains only views, not arena-backed builders.
    run->ModelMem = modelMem;
    run->HasModel = true;
    run->ModelInfo = model;
  }
  if (update->HasProfile) {
    run->HasProfile = true;
    run->Profile = update->Profile;
  }
  if (update->HasContext) {
    AgentRunState_DropContext(run);
    run->ContextSlot = contextSlot;
    run->HasContext = true;
    run->Context = context;
  }
  return SessionResult_Ok();
}

typedef SessionResult (*AgentMessageSourceFn)(void* user, Arena* arena, Array<Message>* out);

SessionResult DrainMessageSource(AgentHarness* harness, AgentMessageSourceFn source,
                                 const char* failure, DurableId* ioHead,
                                 AgentRunState* run, u32* outCount) {
  *outCount = 0;
  if (!source) return SessionResult_Ok();
  Array<Message> messages = Array_Make<Message>(&harness->Work, 4);
  SessionResult hooked = AgentHook_Failure(
      source(harness->Config.Hooks.User, &harness->Work, &messages), failure);
  if (!SessionResult_IsOk(hooked)) return hooked;
  if (messages.Count == 0) return SessionResult_Ok();

  Array<SessionRecord> records = Array_Make<SessionRecord>(&harness->Work, messages.Count);
  DurableId head = *ioHead;
  for (u32 i = 0; i < messages.Count; i++) {
    SessionRecord record = NewMessageRecord(head, messages.Data[i]);
    Array_Push(&records, record);
    head = record.EntryId;
  }
  SessionResult appended = AgentHarness_Append(harness, records.Data, records.Count);
  if (!SessionResult_IsOk(appended)) return appended;
  *ioHead = head;
  *outCount = messages.Count;
  AgentRunState_Record(run, records.Data, records.Count);
  return SessionResult_Ok();
}

SessionResult FlushQueueIntoRun(AgentHarness* harness, Array<Message>* queue, DurableId* ioHead,
                                AgentRunState* run, u32* outCount) {
  Array<Message> drained = Array_Make<Message>(&harness->Work, 4);
  SessionResult result = AgentHarness_FlushQueue(harness, queue, ioHead, outCount, &drained);
  if (!SessionResult_IsOk(result)) return result;
  AgentRunState_RecordMessages(run, drained.Data, drained.Count);
  return SessionResult_Ok();
}

SessionResult FlushSteering(AgentHarness* harness, DurableId* ioHead, AgentRunState* run,
                            u32* outCount) {
  u32 queued = 0;
  SessionResult result =
      FlushQueueIntoRun(harness, &harness->Queues.Steering, ioHead, run, &queued);
  if (!SessionResult_IsOk(result)) return result;
  u32 hooked = 0;
  result = DrainMessageSource(harness, harness->Config.Hooks.GetSteeringMessages,
                              "steering-messages hook failed", ioHead, run, &hooked);
  if (!SessionResult_IsOk(result)) return result;
  *outCount = queued + hooked;
  return SessionResult_Ok();
}

SessionResult FlushFollowUp(AgentHarness* harness, DurableId* ioHead, AgentRunState* run,
                            u32* outCount) {
  u32 queued = 0;
  SessionResult result =
      FlushQueueIntoRun(harness, &harness->Queues.FollowUp, ioHead, run, &queued);
  if (!SessionResult_IsOk(result)) return result;
  u32 hooked = 0;
  result = DrainMessageSource(harness, harness->Config.Hooks.GetFollowUpMessages,
                              "follow-up-messages hook failed", ioHead, run, &hooked);
  if (!SessionResult_IsOk(result)) return result;
  *outCount = queued + hooked;
  return SessionResult_Ok();
}

SessionResult RunAssistantTurn(AgentHarness* harness, AgentTurnKind turnKind, DurableId headId,
                               const Message* extraTrailingMessage,
                               const AgentRunState* run, TurnRunResult* out) {
  Array<Message> contextMessages = {};
  if (run && run->HasContext) {
    contextMessages =
        Array_Make<Message>(&harness->Work, run->Context.Count ? run->Context.Count : 1);
    for (u32 i = 0; i < run->Context.Count; i++)
      Array_Push(&contextMessages, run->Context.Data[i]);
  } else {
    SessionReduction reduction = AgentHarness_ReduceSnapshot(harness);
    SessionResult contextResult =
        SessionReducer_Context(&harness->Work, &reduction, headId, &contextMessages);
    if (!SessionResult_IsOk(contextResult)) return contextResult;
  }
  if (extraTrailingMessage) Array_Push(&contextMessages, *extraTrailingMessage);

  if (harness->Config.Hooks.BeforeContext) {
    OwnMutableMessages(&harness->Work, &contextMessages);
    SessionResult hooked = AgentHook_Failure(
        harness->Config.Hooks.BeforeContext(harness->Config.Hooks.User, &harness->Work,
                                            turnKind, headId, &contextMessages),
        "before-context hook failed");
    if (!SessionResult_IsOk(hooked)) return hooked;
  }

  if (harness->Config.Hooks.ConvertToLlm) {
    OwnMutableMessages(&harness->Work, &contextMessages);
    SessionResult hooked = AgentHook_Failure(
        harness->Config.Hooks.ConvertToLlm(harness->Config.Hooks.User, &harness->Work,
                                           turnKind, headId, &contextMessages),
        "convert-to-llm hook failed");
    if (!SessionResult_IsOk(hooked)) return hooked;
  }

  AgentProviderRequest request = {};
  StrView effectiveSystemPrompt = AgentPermission_AppendSystemPrompt(
      &harness->Work, harness->Config.SystemPrompt,
      AgentPermissionEngine_GetMode(&harness->Permissions));
  AgentProvider_BuildRequest(&harness->Work, effectiveSystemPrompt, contextMessages.Data,
                             contextMessages.Count, harness->Config.Tools,
                             harness->Config.ToolCount, &request);

  for (u32 attemptIndex = 0;; attemptIndex++) {
    AgentHarnessProviderInput effective = {};
    effective.ModelInfo = run && run->HasModel ? run->ModelInfo : harness->Config.ModelInfo;
    effective.Context = request.Context;
    effective.Options = harness->Config.Stream;
    if (run && run->HasProfile) effective.Options.Profile = run->Profile;
    HttpRetryPolicy hookRetry = {};
    if (harness->Config.Hooks.BeforeProvider) {
      OwnMutableProviderInput(&harness->Work, &effective);
      if (effective.Options.Retry) {
        hookRetry = *effective.Options.Retry;
        effective.Options.Retry = &hookRetry;
      }
      SessionResult hooked = AgentHook_Failure(
          harness->Config.Hooks.BeforeProvider(harness->Config.Hooks.User, &harness->Work,
                                               turnKind, attemptIndex, &effective),
          "before-provider hook failed");
      if (!SessionResult_IsOk(hooked)) return hooked;
    }
    if (!ResolveProviderInput(&effective))
      return SessionResult_Error(
          SessionStatus::HookError,
          SVL("effective provider settings are invalid (model, token limit, or profile)"));

    StreamOptions options = effective.Options;
    options.Abort = &harness->AbortFlag;
    HttpRetryPolicy providerRetry = {};
    if (options.Retry) {
      providerRetry = *options.Retry;
      providerRetry.EventUser = harness;
      providerRetry.OnEvent = OnProviderRequestRetry;
      options.Retry = &providerRetry;
    }
    AgentProviderAttemptInput input = {};
    input.Mem = &harness->Work;
    input.ProviderImpl = harness->Config.ProviderImpl;
    input.ModelInfo = &effective.ModelInfo;
    input.Context = &effective.Context;
    input.Options = options;
    input.Sink.User = harness;
    input.Sink.OnEvent = OnProviderStreamEvent;
    input.AuthUser = harness->Config.AuthUser;
    input.ResolveAuth = harness->Config.ResolveAuth;
    AgentProviderAttemptResult attempt = AgentProvider_RunAttempt(&input);

    if (harness->Config.Hooks.AfterProvider) {
      OwnMutableMessage(&harness->Work, &attempt.Assistant);
      SessionResult hooked = AgentHook_Failure(
          harness->Config.Hooks.AfterProvider(harness->Config.Hooks.User, &harness->Work,
                                              turnKind, attemptIndex, &attempt.Assistant),
          "after-provider hook failed");
      if (!SessionResult_IsOk(hooked)) return hooked;
    }

    AgentRetryDecision decision = AgentProvider_RetryDecision(
        &harness->Config.Retry, attemptIndex, attempt.Ok, &attempt.Assistant);
    if (decision.Retry) {
      u32 attemptNumber = attemptIndex + 1;
      AgentHarness_PublishRetry(harness, AgentHarnessEventKind::TransientRetryStart,
                                RetryLayer::AssistantTurn, attemptNumber,
                                harness->Config.Retry.MaxRetries, decision.DelayMs,
                                attempt.Assistant.ErrorMessage);
      bool aborted = AgentProvider_SleepBackoff(decision.DelayMs, &harness->AbortFlag);
      AgentHarness_PublishRetry(harness, AgentHarnessEventKind::TransientRetryEnd,
                                RetryLayer::AssistantTurn, attemptNumber,
                                harness->Config.Retry.MaxRetries, decision.DelayMs,
                                attempt.Assistant.ErrorMessage);
      if (aborted)
        return SessionResult_Error(SessionStatus::Cancelled,
                                   SVL("turn cancelled during retry backoff"));
      continue;
    }

    out->Assistant = attempt.Assistant;
    out->ProviderOk = attempt.Ok;
    return SessionResult_Ok();
  }
}

// Every prepared call gets exactly one result, even on cancellation, so no tool
// call is left without a result before the next provider request.
SessionResult RunToolBatch(AgentHarness* harness, DurableId* ioHead, const Message* assistant,
                          AgentRunState* run, bool* outAnyTool, bool* outAllTerminate,
                          Array<Message>* outToolResults) {
  Arena* arena = &harness->Work;
  Message effectiveAssistant = *assistant;
  effectiveAssistant.Content =
      Array_Make<Content>(arena, assistant->Content.Count ? assistant->Content.Count : 1);
  SessionResult beforeToolFailure = SessionResult_Ok();
  for (u32 i = 0; i < assistant->Content.Count; i++) {
    Content content = assistant->Content.Data[i];
    if (content.Kind == ContentKind::ToolCall && !Atomic_LoadI32(&harness->AbortFlag) &&
        harness->Config.Hooks.RewriteToolArguments &&
        SessionResult_IsOk(beforeToolFailure)) {
      StrView arguments = MutableText(arena, content.ToolArgsJson);
      StrView toolName = MutableText(arena, content.ToolName);
      SessionResult hooked = AgentHook_Failure(
          harness->Config.Hooks.RewriteToolArguments(harness->Config.Hooks.User,
                                                     arena, toolName, &arguments),
          "rewrite-tool-arguments hook failed");
      if (!SessionResult_IsOk(hooked)) {
        beforeToolFailure = hooked;
      } else if (arguments.Length == 0) {
        beforeToolFailure = SessionResult_Error(
            SessionStatus::HookError,
            SVL("rewrite-tool-arguments hook produced empty arguments"));
      } else {
        content.ToolArgsJson = StrView_Dup(arena, arguments);
      }
    }
    Array_Push(&effectiveAssistant.Content, content);
  }

  AgentToolBatchPlan plan = {};
  AgentPermissionGate permissionGate = {};
  permissionGate.Engine = &harness->Permissions;
  if (run) {
    permissionGate.NewMessages = run->NewMessages.Data;
    permissionGate.NewMessageCount = run->NewMessages.Count;
  }
  permissionGate.Abort = &harness->AbortFlag;
  AgentToolBatch_Prepare(arena, harness->RuntimeTools, harness->Config.ToolCount,
                        &effectiveAssistant, harness->Config.ToolExec, &harness->AbortFlag,
                        &harness->Config.Hooks, &permissionGate, &plan);
  if (!SessionResult_IsOk(beforeToolFailure)) {
    for (u32 i = 0; i < plan.Calls.Count; i++) {
      plan.Calls.Data[i].Runnable = false;
      plan.Calls.Data[i].Result = Array_Make<Content>(arena, 1);
      Array_Push(&plan.Calls.Data[i].Result, Content_Text(beforeToolFailure.Message));
      plan.Calls.Data[i].IsError = true;
      plan.Calls.Data[i].Terminate = false;
    }
    plan.RunnableCount = 0;
  }
  if (plan.Calls.Count == 0) {
    *outAnyTool = false;
    *outAllTerminate = true;
    return SessionResult_Ok();
  }

  for (u32 i = 0; i < plan.Calls.Count; i++) {
    const Content* call = plan.Calls.Data[i].Call;
    AgentHarness_PublishToolStart(harness, call->ToolId, call->ToolName,
                                  plan.Calls.Data[i].EffectiveArgumentsJson);
  }

  AgentToolBatchResult results = {};
  AgentToolDispatchGate dispatch = {};
  dispatch.User = harness;
  dispatch.TryStart = TryStartTool;
  AgentToolEventSink events = ToolEvents(harness);
  AgentToolBatch_Run(arena, &plan, harness->Config.MaxParallelTools,
                     &harness->Config.Hooks, &dispatch, &events, &results);

  Array<SessionRecord> messages =
      Array_Make<SessionRecord>(arena, results.Calls.Count ? results.Calls.Count : 1);
  DurableId head = *ioHead;
  for (u32 i = 0; i < results.Calls.Count; i++) {
    SessionRecord message =
        NewMessageRecord(head, AgentToolResult_Materialize(&results.Calls.Data[i]));
    Array_Push(&messages, message);
    head = message.EntryId;
  }
  SessionResult committed = AgentHarness_Append(harness, messages.Data, messages.Count);
  // Committed records now point at the harness's own copies; a failed commit
  // is returned without reading them.
  AgentToolBatch_ReleaseWorkers(&plan);
  if (SessionResult_IsOk(committed)) {
    *ioHead = head;
    *outAnyTool = results.AnyTool;
    *outAllTerminate = results.AllTerminate;
    AgentRunState_Record(run, messages.Data, messages.Count);
    if (outToolResults) {
      for (u32 i = 0; i < messages.Count; i++)
        Array_Push(outToolResults, messages.Data[i].MessageEntry.Value);
    }
  }
  if (!SessionResult_IsOk(committed)) return committed;
  if (!SessionResult_IsOk(beforeToolFailure)) return beforeToolFailure;
  return Atomic_LoadI32(&harness->AbortFlag)
             ? SessionResult_Error(SessionStatus::Cancelled,
                                   SVL("run cancelled during tool execution"))
             : committed;
}

// Only Text blocks form the summary, as in pi.
StrView ExtractSummaryText(Arena* arena, const Message* assistant) {
  StrBuf text = StrBuf_Make(arena, 256);
  for (u32 i = 0; i < assistant->Content.Count; i++) {
    const Content* content = &assistant->Content.Data[i];
    if (content->Kind == ContentKind::Text && content->Text.Length) {
      if (text.Length) StrBuf_AppendChar(&text, '\n');
      StrBuf_Append(&text, content->Text);
    }
  }
  return StrBuf_View(&text);
}

struct CompactionTurnResult {
  AgentTurnOutcome Outcome;
  StrView Error;
  DurableId NewHead;
};

// Publishes only turn-level events so inline compaction never nests an agent lifecycle.
SessionResult PerformCompaction(AgentHarness* harness, DurableId head,
                                DurableId baseEntryId, DurableId throughEntryId,
                                StrView instructions,
                                CompactionTurnResult* out) {
  AgentHarness_PublishTurnStart(harness, AgentTurnKind::Compaction);

  StrView summarizeText =
      SVL("Summarize the conversation for future continuation. Preserve goals, "
          "decisions, constraints, unresolved work, and important tool results.");
  if (instructions.Length) {
    // pi's separator, byte for byte.
    StrBuf directed =
        StrBuf_Make(&harness->Work, summarizeText.Length + instructions.Length + 24);
    StrBuf_Append(&directed, summarizeText);
    StrBuf_AppendCStr(&directed, "\n\nAdditional focus: ");
    StrBuf_Append(&directed, instructions);
    summarizeText = StrBuf_View(&directed);
  }
  Message summarizePrompt = Message_User(&harness->Work, summarizeText);

  // Only the folded span: including the retained tail would duplicate it.
  AgentRunState span = {};
  span.HasContext = true;
  SessionReduction reduction = AgentHarness_ReduceSnapshot(harness);
  SessionResult result = SessionReducer_ContextThrough(&harness->Work, &reduction, head,
                                                       throughEntryId, &span.Context);
  TurnRunResult turn = {};
  if (SessionResult_IsOk(result))
    result = RunAssistantTurn(harness, AgentTurnKind::Compaction, head, &summarizePrompt,
                              &span, &turn);

  AgentTurnOutcome outcome = AgentTurnOutcome::Succeeded;
  StrView error = {};

  if (SessionResult_IsOk(result)) {
    bool success = turn.ProviderOk && turn.Assistant.Stop != StopReason::Error &&
                   turn.Assistant.Stop != StopReason::Aborted;
    StrView summaryText =
        success ? ExtractSummaryText(&harness->Work, &turn.Assistant) : StrView{};
    if (!success || summaryText.Length == 0) {
      bool aborted = turn.Assistant.Stop == StopReason::Aborted;
      error = summaryText.Length == 0 && success
                  ? SVL("compaction provider returned an empty summary")
                  : turn.Assistant.ErrorMessage;
      result = SessionResult_Error(
          aborted ? SessionStatus::Cancelled : SessionStatus::ProviderError, error);
    } else {
      AgentFileLists files = {};
      reduction = AgentHarness_ReduceSnapshot(harness);
      Array<const SessionRecord*> branch = {};
      if (SessionResult_IsOk(
              SessionReducer_Ancestry(&reduction, &harness->Work, head, &branch))) {
        AgentFileOps ops = {};
        AgentCompaction_CollectFileOps(&harness->Work, branch.Data, branch.Count,
                                       throughEntryId, &ops);
        AgentCompaction_BuildFileLists(&harness->Work, &ops, &files);
      }

      StrBuf wrapped = StrBuf_Make(&harness->Work, summaryText.Length + 48);
      StrBuf_AppendCStr(&wrapped, "Summary of previous conversation:\n");
      StrBuf_Append(&wrapped, summaryText);
      AgentCompaction_AppendFileSections(&wrapped, &files);
      Message summary = Message_User(&harness->Work, StrBuf_View(&wrapped));

      SessionRecord compactionRecord = {};
      compactionRecord.EntryId = DurableId_Generate();
      compactionRecord.Kind = SessionRecordKind::Compaction;
      compactionRecord.CompactionEntry.ParentId = head;
      compactionRecord.CompactionEntry.BaseEntryId = baseEntryId;
      compactionRecord.CompactionEntry.ThroughEntryId = throughEntryId;
      compactionRecord.CompactionEntry.SourceHeadId = head;
      compactionRecord.CompactionEntry.Summary = summary;
      compactionRecord.CompactionEntry.HasUsage = true;
      compactionRecord.CompactionEntry.Use = turn.Assistant.Use;
      compactionRecord.CompactionEntry.ReadFiles = files.ReadFiles;
      compactionRecord.CompactionEntry.ModifiedFiles = files.ModifiedFiles;
      result = AgentHarness_Append(harness, &compactionRecord, 1, true);
      if (SessionResult_IsOk(result)) out->NewHead = compactionRecord.EntryId;
    }
  }

  if (!SessionResult_IsOk(result)) {
    outcome = result.Status == SessionStatus::Cancelled ? AgentTurnOutcome::Aborted
                                                        : AgentTurnOutcome::Failed;
    if (!error.Length) error = result.Message;
  }
  AgentHarness_PublishTurnEnd(harness, AgentTurnKind::Compaction, outcome, error);
  out->Outcome = outcome;
  out->Error = error;
  return result;
}

u32 ActiveContextWindow(const AgentHarness* harness, const AgentRunState* run) {
  u32 window = run && run->HasProfile ? run->Profile.ContextWindow
                                      : harness->Config.Stream.Profile.ContextWindow;
  if (window) return window;
  return run && run->HasModel ? run->ModelInfo.ContextWindow
                              : harness->Config.ModelInfo.ContextWindow;
}

// The cut point always comes from the durable branch, which is what compaction folds.
bool ShouldAutoCompact(AgentHarness* harness, DurableId head, const AgentRunState* run,
                       AgentCompactionCut* outCut) {
  const AgentCompactionPolicy* policy = &harness->Config.Compaction;
  u32 window = ActiveContextWindow(harness, run);
  if (!AgentCompactionPolicy_IsValid(policy) || !window) return false;

  SessionReduction reduction = AgentHarness_ReduceSnapshot(harness);
  u32 tokens = 0;
  if (run && run->HasContext) {
    // A hook-supplied context breaks the reported-usage anchor's invariant; only the
    // byte estimate is meaningful.
    tokens = AgentCompaction_EstimateContextTokens(run->Context.Data, run->Context.Count);
  } else {
    Array<Message> messages = {};
    if (!SessionResult_IsOk(
            SessionReducer_Context(&harness->Work, &reduction, head, &messages)))
      return false;
    tokens = AgentCompaction_ContextTokens(messages.Data, messages.Count);
  }
  if (!AgentCompaction_ShouldCompact(policy, window, tokens)) return false;

  Array<const SessionRecord*> branch = {};
  if (!SessionResult_IsOk(
          SessionReducer_Ancestry(&reduction, &harness->Work, head, &branch)))
    return false;
  return AgentCompaction_SelectCutPoint(branch.Data, branch.Count,
                                        policy->KeepRecentTokens, outCut);
}

SessionResult BuildTurnContext(AgentHarness* harness, DurableId head, const Message* assistant,
                               Array<Message> toolResults, const AgentRunState* run,
                               AgentTurnContext* out) {
  SessionReduction reduction = AgentHarness_ReduceSnapshot(harness);
  Array<Message> context = {};
  SessionResult result = SessionReducer_Context(&harness->Work, &reduction, head, &context);
  if (!SessionResult_IsOk(result)) return result;
  out->Turn = AgentTurnKind::Run;
  out->HeadId = head;
  out->Assistant = assistant;
  out->ToolResults = toolResults;
  out->Context = context;
  if (run) out->NewMessages = run->NewMessages;
  return SessionResult_Ok();
}

// Called with no harness lock held: this blocks until the user answers.
bool AskToContinuePastTurnLimit(AgentHarness* harness, u32 turnsUsed) {
  StrBuf question = StrBuf_Make(&harness->Work, 160);
  StrBuf_AppendCStr(&question, "The agent has run ");
  StrBuf_AppendU64(&question, turnsUsed);
  StrBuf_AppendCStr(&question,
                    " turns in a row without input. Keep going?");

  StrView choices[] = {SVL("Continue"), SVL("Stop")};
  AgentAskRequest request = {};
  request.Question = StrBuf_View(&question);
  request.Choices = choices;
  request.ChoiceCount = 2;

  AgentAskResponse response =
      AgentAskUser_Invoke(&harness->Config.AskUser, &harness->Work, &request);
  return response.Status == AgentAskStatus::Answered && response.ChoiceIndex == 0;
}

SessionResult ContinueRun(AgentHarness* harness, DurableId* ioHead, AgentRunState* run) {
  for (;;) {
    if (Atomic_LoadI32(&harness->AbortFlag))
      return SessionResult_Error(SessionStatus::Cancelled, SVL("run cancelled"));

    // Nothing allocated from Work survives an iteration; anything spanning turns
    // lives in RunMem or the run's override owners.
    ArenaReset(&harness->Work);
    ArenaTrim(&harness->Work, WORK_ARENA_KEEP_BYTES);

    if (run && AgentTurnLimit_Reached(&harness->Config.TurnLimit,
                                      run->AutonomousTurns - run->GrantedTurns)) {
      u32 spent = run->AutonomousTurns;
      if (!AskToContinuePastTurnLimit(harness, spent)) {
        StrBuf notice = StrBuf_Make(&harness->Work, 96);
        StrBuf_AppendCStr(&notice, "stopped after ");
        StrBuf_AppendU64(&notice, spent);
        StrBuf_AppendCStr(&notice, " turns without input");
        // Graceful stop: Ok with a message, which CompleteAgentTurnLocked copies out of Work.
        return SessionResult{SessionStatus::Ok, StrBuf_View(&notice), 0};
      }
      run->GrantedTurns = spent;
    }
    if (run) run->AutonomousTurns++;

    // At most once per iteration; if still over budget, continue rather than loop.
    AgentCompactionCut cut = {};
    if (ShouldAutoCompact(harness, *ioHead, run, &cut)) {
      if (harness->Config.Hooks.BeforeCompaction) {
        SessionResult hooked = AgentHook_Failure(
            harness->Config.Hooks.BeforeCompaction(harness->Config.Hooks.User,
                                                   &harness->Work, *ioHead),
            "before-compaction hook failed");
        if (!SessionResult_IsOk(hooked)) return hooked;
      }
      CompactionTurnResult compaction = {};
      // Automatic compaction never inherits `/compact` instructions, as in pi.
      SessionResult compacted = PerformCompaction(harness, *ioHead, cut.BaseEntryId,
                                                  cut.ThroughEntryId, StrView{},
                                                  &compaction);
      if (!SessionResult_IsOk(compacted)) {
        if (compacted.Status == SessionStatus::Cancelled) return compacted;
        StrBuf message = StrBuf_Make(&harness->Work, compacted.Message.Length + 40);
        StrBuf_AppendCStr(&message, "automatic compaction failed");
        if (compacted.Message.Length) {
          StrBuf_AppendCStr(&message, ": ");
          StrBuf_Append(&message, compacted.Message);
        }
        return SessionResult_Error(compacted.Status, StrBuf_View(&message));
      }
      *ioHead = compaction.NewHead;
      // Compaction made the override stale and it cannot be rebased.
      if (run) AgentRunState_DropContext(run);
    }

    AgentHarness_PublishTurnStart(harness, AgentTurnKind::Run);
    TurnRunResult turn = {};
    SessionResult generated =
        RunAssistantTurn(harness, AgentTurnKind::Run, *ioHead, nullptr, run, &turn);
    if (!SessionResult_IsOk(generated)) {
      bool cancelled = generated.Status == SessionStatus::Cancelled;
      AgentHarness_PublishTurnEnd(harness, AgentTurnKind::Run,
                                 cancelled ? AgentTurnOutcome::Aborted : AgentTurnOutcome::Failed,
                                 generated.Message);
      return generated;
    }

    // Every stop reason becomes exactly one durable assistant message.
    SessionRecord assistantRecord = NewMessageRecord(*ioHead, turn.Assistant);
    SessionResult committed = AgentHarness_Append(harness, &assistantRecord, 1);
    if (!SessionResult_IsOk(committed)) {
      AgentHarness_PublishTurnEnd(harness, AgentTurnKind::Run, AgentTurnOutcome::Failed,
                                 committed.Message);
      return committed;
    }
    *ioHead = assistantRecord.EntryId;
    AgentRunState_Record(run, &assistantRecord, 1);

    if (!turn.ProviderOk || turn.Assistant.Stop == StopReason::Error ||
        turn.Assistant.Stop == StopReason::Aborted) {
      bool aborted = turn.Assistant.Stop == StopReason::Aborted;
      AgentHarness_PublishTurnEnd(harness, AgentTurnKind::Run,
                                 aborted ? AgentTurnOutcome::Aborted : AgentTurnOutcome::Failed,
                                 turn.Assistant.ErrorMessage);
      return SessionResult_Error(aborted ? SessionStatus::Cancelled : SessionStatus::ProviderError,
                                 turn.Assistant.ErrorMessage);
    }

    // The provider never closes a truncated tool-call block, so the batch would see
    // no tools and end the run as though the model had finished.
    if (turn.Assistant.Stop == StopReason::Length) {
      StrView truncated =
          SVL("assistant response hit the output token limit before completing");
      AgentHarness_PublishTurnEnd(harness, AgentTurnKind::Run, AgentTurnOutcome::Failed,
                                 truncated);
      return SessionResult_Error(SessionStatus::ProviderError, truncated);
    }

    bool anyTool = false;
    bool allTerminate = true;
    Array<Message> toolResults = Array_Make<Message>(&harness->Work, 4);
    SessionResult tools = RunToolBatch(harness, ioHead, &turn.Assistant, run, &anyTool,
                                       &allTerminate, &toolResults);
    if (!SessionResult_IsOk(tools)) {
      AgentHarness_PublishTurnEnd(harness, AgentTurnKind::Run,
                                 tools.Status == SessionStatus::Cancelled
                                     ? AgentTurnOutcome::Aborted
                                     : AgentTurnOutcome::Failed,
                                 tools.Message);
      return tools;
    }
    AgentHarness_PublishTurnEnd(harness, AgentTurnKind::Run, AgentTurnOutcome::Succeeded, StrView{});

    // pi's order: prepareNextTurn, then shouldStopAfterTurn, before steering is polled.
    if (harness->Config.Hooks.PrepareNextTurn || harness->Config.Hooks.ShouldStopAfterTurn) {
      AgentTurnContext turnContext = {};
      SessionResult built = BuildTurnContext(harness, *ioHead, &assistantRecord.MessageEntry.Value,
                                             toolResults, run, &turnContext);
      if (!SessionResult_IsOk(built)) return built;

      if (harness->Config.Hooks.PrepareNextTurn) {
        OwnMutableTurnContext(&harness->Work, &turnContext);
        AgentNextTurnUpdate update = {};
        // Construction scratch only; Apply copies accepted fields before Work is recycled.
        SessionResult hooked = AgentHook_Failure(
            harness->Config.Hooks.PrepareNextTurn(harness->Config.Hooks.User, &harness->Work,
                                                  &turnContext, &update),
            "prepare-next-turn hook failed");
        if (!SessionResult_IsOk(hooked)) return hooked;
        SessionResult applied = AgentRunState_Apply(run, &update);
        if (!SessionResult_IsOk(applied)) return applied;
        // pi hands shouldStopAfterTurn the already-updated context.
        if (run && run->HasContext) turnContext.Context = run->Context;
      }

      if (harness->Config.Hooks.ShouldStopAfterTurn) {
        OwnMutableTurnContext(&harness->Work, &turnContext);
        bool stop = false;
        SessionResult hooked = AgentHook_Failure(
            harness->Config.Hooks.ShouldStopAfterTurn(harness->Config.Hooks.User, &harness->Work,
                                                      &turnContext, &stop),
            "should-stop-after-turn hook failed");
        if (!SessionResult_IsOk(hooked)) return hooked;
        if (stop) return SessionResult_Ok();
      }
    }

    u32 steerCount = 0;
    SessionResult steered = FlushSteering(harness, ioHead, run, &steerCount);
    if (!SessionResult_IsOk(steered)) return steered;
    // Only user-driven inputs refill the budget; resetting on any append would make
    // the cap unreachable.
    if (steerCount && run) {
      run->AutonomousTurns = 0;
      run->GrantedTurns = 0;
    }
    if (AgentCheckpoint_ContinueTurn(anyTool, allTerminate, steerCount)) continue;

    u32 followUpCount = 0;
    SessionResult followed = FlushFollowUp(harness, ioHead, run, &followUpCount);
    if (!SessionResult_IsOk(followed)) return followed;
    if (followUpCount && run) {
      run->AutonomousTurns = 0;
      run->GrantedTurns = 0;
    }
    if (AgentCheckpoint_ContinueRun(followUpCount)) continue;

    return SessionResult_Ok();
  }
}

}

bool AgentDriver_ReviewPermission(
    void* user, Arena* arena, const AgentPermissionReviewRequest* request,
    volatile i32* abort, AgentPermissionReviewResult* out) {
  if (out) {
    *out = {};
  }
  AgentHarness* harness = (AgentHarness*)user;
  if (!harness || !arena || !request || !out) return false;
  StrView reviewerModel = harness->Config.PermissionReviewerModel;
  AgentHarness_PublishPermissionReviewStart(harness, request->ToolName,
                                            reviewerModel);
  const AgentPermissionReviewer* reviewer = &harness->Config.PermissionReviewer;
  bool ok = reviewer->Review &&
            reviewer->Review(reviewer->User, arena, request, abort, out);
  AgentPermissionReviewOutcome outcome = AgentPermissionReviewOutcome::Ask;
  if (!ok) {
    out->Decision = AgentPermissionReviewDecision::Ask;
    outcome = AgentPermissionReviewOutcome::Unavailable;
    if (!out->Rationale.Length)
      out->Rationale = SVL("Automatic permission review was unavailable or invalid.");
  } else if (out->Decision == AgentPermissionReviewDecision::Allow) {
    outcome = AgentPermissionReviewOutcome::Allow;
  }
  AgentHarness_PublishPermissionReviewEnd(
      harness, request->ToolName, out->Model.Length ? out->Model : reviewerModel,
      outcome, out->Rationale);
  return ok;
}

SessionResult AgentDriver_Run(AgentHarness* harness, StrView prompt) {
  if (prompt.Length == 0)
    return SessionResult_Error(SessionStatus::InvalidArgument,
                               SVL("run prompt must not be empty"));
  AgentHarness_PublishAgentStart(harness, AgentTurnKind::Run);

  DurableId head = AgentHarness_ReduceSnapshot(harness).HeadId;
  AgentRunState run = {};
  run.NewMessages = Array_Make<Message>(&harness->RunMem, 8);
  u32 nextTurnCount = 0;
  SessionResult result =
      FlushQueueIntoRun(harness, &harness->Queues.NextTurn, &head, &run, &nextTurnCount);

  if (SessionResult_IsOk(result)) {
    Message accepted = Message_User(&harness->Work, StrView_Dup(&harness->Work, prompt));
    if (harness->Config.Hooks.BeforeRun) {
      result = AgentHook_Failure(
          harness->Config.Hooks.BeforeRun(harness->Config.Hooks.User, &harness->Work, &accepted),
          "before-run hook failed");
    }
    if (SessionResult_IsOk(result)) {
      SessionRecord userRecord = NewMessageRecord(head, accepted);
      result = AgentHarness_Append(harness, &userRecord, 1);
      if (SessionResult_IsOk(result)) {
        head = userRecord.EntryId;
        AgentRunState_Record(&run, &userRecord, 1);
      }
    }
  }

  // pi polls steering once before the first turn.
  if (SessionResult_IsOk(result)) {
    u32 steerCount = 0;
    result = FlushSteering(harness, &head, &run, &steerCount);
  }

  if (SessionResult_IsOk(result)) result = ContinueRun(harness, &head, &run);

  AgentTurnOutcome outcome = AgentTurnOutcome::Succeeded;
  StrView error = {};
  if (!SessionResult_IsOk(result)) {
    outcome =
        result.Status == SessionStatus::Cancelled ? AgentTurnOutcome::Aborted : AgentTurnOutcome::Failed;
    error = result.Message;
  }
  if (harness->Config.Hooks.AfterTurn) {
    SessionResult afterTurn = AgentHook_Failure(
        harness->Config.Hooks.AfterTurn(harness->Config.Hooks.User, &harness->Work,
                                        AgentTurnKind::Run, outcome,
                                        MutableText(&harness->Work, error)),
        "after-turn hook failed");
    if (SessionResult_IsOk(result) && !SessionResult_IsOk(afterTurn)) {
      result = afterTurn;
      outcome = AgentTurnOutcome::Failed;
      error = result.Message;
    }
  }
  // Any hook/provider error (or successful stop notice) can borrow an override.
  // CompleteAgentTurnLocked copies from Work only after the driver returns.
  if (result.Message.Length && AgentRunState_OwnsText(&run, result.Message))
    result.Message = StrView_Dup(&harness->Work, result.Message);
  AgentRunState_DropContext(&run);
  ArenaRelease(&run.ModelMem);
  return result;
}

SessionResult AgentDriver_Compact(AgentHarness* harness, StrView instructions) {
  AgentHarness_PublishAgentStart(harness, AgentTurnKind::Compaction);
  SessionReduction initial = AgentHarness_ReduceSnapshot(harness);
  DurableId head = initial.HeadId;

  SessionResult result = SessionResult_Ok();
  if (harness->Config.Hooks.BeforeCompaction) {
    result = AgentHook_Failure(
        harness->Config.Hooks.BeforeCompaction(harness->Config.Hooks.User, &harness->Work, head),
        "before-compaction hook failed");
  }

  AgentTurnOutcome outcome = AgentTurnOutcome::Failed;
  StrView error = {};

  if (SessionResult_IsOk(result)) {
    CompactionTurnResult compaction = {};
    result = PerformCompaction(
        harness, head, initial.RecordCount ? initial.Records[0].EntryId : DurableId{},
        head, instructions, &compaction);
    outcome = compaction.Outcome;
    error = compaction.Error;
  } else {
    error = result.Message;
  }

  if (harness->Config.Hooks.AfterTurn) {
    SessionResult afterTurn = AgentHook_Failure(
        harness->Config.Hooks.AfterTurn(harness->Config.Hooks.User, &harness->Work,
                                        AgentTurnKind::Compaction, outcome,
                                        MutableText(&harness->Work, error)),
        "after-turn hook failed");
    if (SessionResult_IsOk(result) && !SessionResult_IsOk(afterTurn)) {
      result = afterTurn;
      outcome = AgentTurnOutcome::Failed;
      error = result.Message;
    }
  }
  return result;
}
