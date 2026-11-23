#pragma once

#include "agent/tool.h"
#include "session/reducer.h"

struct ToolOutcome {
  ContentArray Content;
  bool IsError;
  bool Terminate;
};

// Zero-initialized means "allow". `Terminate` only ends the run when every
// call in the batch sets it.
struct AgentToolGateDecision {
  bool Block;
  StrView Reason;
  bool Terminate;
};

// Model-visible; byte-for-byte parity with pi.
constexpr StrView AGENT_TOOL_BLOCKED_FALLBACK = SVL("Tool execution was blocked");

enum class AgentTurnKind : u8 { Run, Compaction };

enum class AgentTurnOutcome : u8 { Succeeded, Failed, Aborted };

struct AgentHarnessProviderInput {
  Model ModelInfo;
  LlmContext Context;
  StreamOptions Options;
};

// Arrays live in turn scratch and are valid only during the call. Mutating
// this does not override anything; return AgentNextTurnUpdate instead.
struct AgentTurnContext {
  AgentTurnKind Turn;
  DurableId HeadId;
  const Message* Assistant;
  Array<Message> ToolResults;
  Array<Message> Context;
  Array<Message> NewMessages;
};

// Overrides expire with the run. A replacement Context keeps growing with
// later appends (as pi's currentContext) and is dropped if compaction fires.
struct AgentNextTurnUpdate {
  bool HasContext;
  Array<Message> Context;
  bool HasModel;
  Model ModelInfo;
  bool HasProfile;
  ModelRequestProfile Profile;
};

struct AgentHarnessHooks {
  void* User;

  // Inputs and arena are turn-scoped; borrowed replacement fields must stay
  // readable through this turn. Never modify opaque pointees or the abort flag.
  SessionResult (*BeforeRun)(void* user, Arena* arena, Message* ioPrompt);
  SessionResult (*BeforeContext)(void* user, Arena* arena,
                                 AgentTurnKind turn,
                                 DurableId headId,
                                 Array<Message>* ioMessages);
  SessionResult (*ConvertToLlm)(void* user, Arena* arena,
                                AgentTurnKind turn,
                                DurableId headId,
                                Array<Message>* ioMessages);
  SessionResult (*BeforeProvider)(void* user, Arena* arena,
                                  AgentTurnKind turn,
                                  u32 attemptIndex,
                                  AgentHarnessProviderInput* io);
  SessionResult (*AfterProvider)(void* user, Arena* arena,
                                 AgentTurnKind turn,
                                 u32 attemptIndex,
                                 Message* ioAssistant);
  SessionResult (*RewriteToolArguments)(void* user, Arena* arena,
                                        StrView toolName,
                                        StrView* ioArgumentsJson);
  // Runs serially on the coordinator thread before any call in the batch starts.
  void (*BeforeTool)(void* user, Arena* arena, StrView toolName,
                     StrView toolCallId, const ToolArgs* args,
                     AgentToolGateDecision* out);
  // Coordinator thread, before the result is durable.
  void (*AfterTool)(void* user, Arena* arena, StrView toolName,
                    StrView toolCallId, const ToolArgs* args, ToolOutcome* io);
  SessionResult (*PrepareNextTurn)(void* user, Arena* arena,
                                   const AgentTurnContext* context,
                                   AgentNextTurnUpdate* out);
  SessionResult (*ShouldStopAfterTurn)(void* user, Arena* arena,
                                       const AgentTurnContext* context,
                                       bool* outStop);
  SessionResult (*GetSteeringMessages)(void* user, Arena* arena,
                                       Array<Message>* outMessages);
  SessionResult (*GetFollowUpMessages)(void* user, Arena* arena,
                                       Array<Message>* outMessages);
  SessionResult (*BeforeCompaction)(void* user, Arena* arena,
                                    DurableId headId);
  SessionResult (*BeforeNavigation)(void* user, Arena* arena,
                                    DurableId* ioTargetEntryId);
  SessionResult (*AfterTurn)(void* user, Arena* arena,
                             AgentTurnKind turn,
                             AgentTurnOutcome outcome,
                             StrView error);
};
