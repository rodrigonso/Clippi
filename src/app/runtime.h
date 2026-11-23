#pragma once

#include "agent/ask.h"
#include "agent/harness.h"
#include "app/command.h"
#include "app/extensions.h"
#include "app/project_trust.h"
#include "app/prompt_context.h"
#include "app/provider_registry.h"
#include "auth/resolve.h"
#include "base/str.h"
#include "session/jsonl_repository.h"
#include "session/repository.h"
#include "tools/tools.h"
#include "tui/tui.h"

struct HarnessEventPump;
struct MemoryExtension;

struct AppAuth {
  Arena* Mem;
  StrView Path;
  AuthTokenCache Cache;
};

struct HarnessEventSink {
  void* User;
  void (*OnEvent)(void* user, const AgentHarnessEvent* ev);
};

inline void HarnessEventSink_Emit(const HarnessEventSink& sink,
                                  const AgentHarnessEvent* ev) {
  if (sink.OnEvent) sink.OnEvent(sink.User, ev);
}

struct InteractiveRuntime {
  Arena* Mem;
  // Reset at every dispatch: only values consumed before the next one may live
  // here; anything installed on the runtime or harness belongs on Mem.
  Arena CommandMem;
  StrView AuthPath;
  StrView SettingsPath;
  const ProviderSpec* Spec;
  Model CurrentModel;
  ModelRequestProfile PreferredProfile;
  AgentHarness* Harness;
  SessionRepository* Sessions;
  // Null under `--no-session`.
  JsonlRepository* JsonlSessions;
  StrView ProjectKey;
  StrView WorkingDirectory;
  StrView AgentDirectory;
  StrView TrustPath;
  bool NoSession;
  // Reported to hooks so a gate can fail closed when nobody could approve.
  bool Interactive;
  bool ProjectTrusted;
  bool HasProjectResources;
  bool TrustCoversProjectResources;
  ProjectTrustOverride TrustOverride;
  DefaultProjectTrust DefaultTrust;
  AgentPermissionMode PermissionMode = AgentPermissionMode::Auto;
  AgentPermissionMode PersistedPermissionMode = AgentPermissionMode::Auto;
  StrView PermissionReviewerModel = AGENT_PERMISSION_DEFAULT_REVIEWER_MODEL;
  f64 PermissionReviewerThreshold = AGENT_PERMISSION_DEFAULT_REVIEWER_THRESHOLD;
  bool YoloAcknowledged = false;
  // Keeps a one-run CLI mode from leaking into unrelated settings saves.
  bool PermissionModeOverridden = false;
  bool Rounded = true;
  PromptContext* Prompt;
  PromptContextOptions PromptOptions;
  // Allocated from CommandMem: consume it before dispatching anything else.
  StrView PendingPrompt;
  HarnessEventPump* EventPump;
  HarnessEventSink EventSink;
  Tui* TuiView;
  AppAuth* Auth;
  AppExtensions* Extensions;
  // Null when memory is off for this run.
  MemoryExtension* Memory;
  // The persisted setting, which a run-level override leaves untouched.
  bool MemoryEnabled = true;
  StrView DreamModel;
};

StrView RuntimeMemoryStore(const InteractiveRuntime* rt, bool enabled);

bool RuntimeRebuildPrompt(InteractiveRuntime* rt, StrBuf* error);

struct ShellDirectoryExtension {
  ShellToolContext* Shell;
  Arena Mem;
};

AppExtension ShellDirectoryExtension_Make(ShellDirectoryExtension* state,
                                          ShellToolContext* shell);

void RuntimeAnnounceSession(InteractiveRuntime* rt, StrView projectKey,
                            DurableId sessionId);

// One session per process; the signal handler has no other way to reach it.

extern volatile i32 g_abort;
extern AgentHarness* g_activeHarness;

SessionResult RunHarnessPrompt(AgentHarness* harness, StrView prompt);

void PlainCommandNotice(void* user, StrView message);
void PlainCommandError(void* user, StrView message);
void TuiCommandNotice(void* user, StrView message);
void TuiCommandError(void* user, StrView message);
void EmitCommandNotice(const AppCommandUx* ux, StrView message);
void EmitCommandError(const AppCommandUx* ux, StrView message);

enum class RuntimeInputResult : u8 { Confirmed, Back, Cancelled, Failed };
enum class RuntimeChoiceStatus : u8 { Selected, Back, Cancelled, Failed };

RuntimeInputResult RuntimeSelect(InteractiveRuntime* rt, StrView title,
                                 const StrView* options, u32 optionCount,
                                 u32* selected, u32 initialSelection = 0,
                                 StrView category = SVL("Commands"));
RuntimeInputResult RuntimeReadText(InteractiveRuntime* rt, Arena* arena,
                                   StrView title, StrView prompt,
                                   StrView initial, StrView* out,
                                   StrView category = SVL("Commands"),
                                   TextInputMode mode = TextInputMode::Plain);

// Safe from a tool worker thread: the TUI's input thread serves the modal, and
// the plain REPL's main thread is parked in AgentHarness_Run.
AgentAskUser RuntimeAskUser(InteractiveRuntime* rt);

volatile i32* RuntimeBeginOperation(InteractiveRuntime* rt, const AppCommandUx* ux,
                                    StrView label, bool yieldsToInput = false);
void RuntimeSetOperationStatus(InteractiveRuntime* rt, const AppCommandUx* ux,
                               StrView status);
bool RuntimeEndOperation(InteractiveRuntime* rt);

AppCommandHandlerResult CommandResult(SessionResult result, StrView successMessage);
AppCommandHandlerResult RuntimeOwnedFailure(InteractiveRuntime* rt, StrView message);
AppCommandHandlerResult ReturnToCommandPalette(InteractiveRuntime* rt);

// Every InteractiveRuntime must go through this: uncreated arenas trap.
void InteractiveRuntime_Init(InteractiveRuntime* rt, Arena* mem);
void InteractiveRuntime_Release(InteractiveRuntime* rt);
void RuntimeSetPromptOptions(InteractiveRuntime* rt,
                             const PromptContextOptions* options);
bool RuntimePersistSettings(InteractiveRuntime* rt);
bool RuntimeRefreshPermissionReviewer(InteractiveRuntime* rt);

// Use this, not AppCommand_Dispatch, so CommandMem cannot grow.
AppCommandDispatchResult RuntimeDispatchCommand(InteractiveRuntime* rt, StrView input,
                                                const AppCommandHandlers* handlers,
                                                const AppCommandUx* ux);

StrView PromptContextReport(Arena* arena, const PromptContext* context,
                            bool hasProjectResources, bool projectTrusted,
                            StrView heading);
void EmitPromptDiagnostics(const PromptContext* context, const AppCommandUx* ux);
void PrintPromptDiagnostics(const PromptContext* context);
