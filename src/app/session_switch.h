#pragma once

#include "app/runtime.h"

// RuntimeSwitchToLog moves everything together and rolls back on failure.

enum class TrustChoice : u8 {
  TrustCurrent,
  TrustParent,
  TrustSession,
  UntrustCurrent,
  UntrustSession,
};

constexpr u32 TRUST_CHOICE_CAPACITY = 5;

struct TrustDecision {
  bool Trusted;
  bool Persist;  // false for the session-only choices
  StrView DecisionPath;
  StrView RemovePath;  // a parent-scoped decision supersedes any child entry
};

struct SessionOptionList {
  SessionInfo** Sessions;
  StrView* Options;
  u32 Count;
};

// Each out array must hold at least TRUST_CHOICE_CAPACITY entries.
u32 BuildTrustChoices(Arena* arena, StrView parent, bool includeSessionOnly,
                      StrView* outOptions, TrustChoice* outChoices);
TrustDecision ResolveTrustChoice(TrustChoice choice, StrView cwd, StrView parent);

SessionResult ResolveRuntimeProjectTrust(InteractiveRuntime* rt, Arena* scratch,
                                         StrView workingDirectory, bool* trusted,
                                         bool* hasResources);

bool ResolveStartupProjectTrust(Arena* arena, StrView trustPath,
                                StrView workingDirectory,
                                DefaultProjectTrust defaultTrust,
                                ProjectTrustOverride overrideValue, bool interactive,
                                bool* trusted, bool* hasResources);

bool RuntimeRestartWatch(InteractiveRuntime* rt, const AppCommandUx* ux);
bool RuntimeRestorePreviousSession(InteractiveRuntime* rt, DurableId previousSessionId,
                                   const AgentHarnessConfig* config,
                                   const AppCommandUx* ux);
bool RuntimeRollbackSessionSwitch(InteractiveRuntime* rt,
                                  StrView previousWorkingDirectory,
                                  DurableId previousSessionId,
                                  const AgentHarnessConfig* previousConfig,
                                  const AppCommandUx* ux);
SessionResult RuntimeSwitchToLog(InteractiveRuntime* rt, SessionLog target,
                                 const SessionInfo* targetInfo,
                                 const AppCommandUx* ux);

StrView FormatLocalTime(Arena* arena, u64 timestampMs);
StrView SessionTitle(const SessionInfo* info);
StrView SessionStatusText(const SessionInfo* info);
Array<StrView> SessionDetailLines(Arena* arena, const SessionInfo* info,
                                  StrView storage, StrView status);
StrView FormatSessionOption(Arena* arena, const SessionInfo* info,
                            DurableId currentSessionId, bool allProjects,
                            bool includeFullId, u64 nowMs, StrView home);
SessionOptionList BuildSessionOptions(Arena* arena, Array<SessionInfo>* items,
                                      DurableId currentSessionId, bool allProjects,
                                      bool includeFullId, bool availableOnly,
                                      u64 nowMs, StrView home);
SessionResult CurrentSessionId(Arena* arena, InteractiveRuntime* rt, DurableId* out);
