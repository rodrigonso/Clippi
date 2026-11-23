#pragma once

#include "app/runtime.h"
#include "app/settings.h"

struct RuntimeSelection {
  const ProviderSpec* Spec;
  StrView ModelId;
  AppSettings Settings;
};

void AppAuth_Invalidate(AppAuth* auth);

// `user` is an AppAuth*.
bool AppResolveAuth(void* user, Arena* arena, StrView provider, volatile i32* abort,
                    ProviderAuth* out);

bool ResolvePromptAuth(AgentHarness* harness, volatile i32* abort);

StrView AuthGuidance(Arena* arena, const ProviderSpec* spec, bool interactive);

RuntimeSelection LoadRuntimeSelection(Arena* arena, StrView settingsPath,
                                      const char* modelOverride);

bool RuntimeCheckAuth(InteractiveRuntime* rt, const AppCommandUx* ux, bool asError);

AppCommandHandlerResult RuntimeEnterApiKey(
    InteractiveRuntime* rt, StrView credentialId, StrView title,
    StrView category, const AppCommandUx* ux);

bool ActivateRuntimeSelection(InteractiveRuntime* rt, const ProviderSpec* spec,
                              const Model& model, ModelRequestProfile preference,
                              const AppCommandUx* ux, bool* persisted = nullptr);
StrView SelectionMessage(InteractiveRuntime* rt, const char* prefix);

AppCommandHandlerResult HandleModelCommand(void* user, const AppCommandUx* ux);
AppCommandHandlerResult HandleAuthCommand(void* user, const AppCommandUx* ux);

int RunCommandLineLogout(Arena* arena, StrView authPath, const char* providerId);
