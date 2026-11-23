#pragma once

#include "agent/harness_event.h"
#include "agent/hook_chain.h"
#include "base/str.h"

// Views are valid only during the call; an extension copies what it keeps.
struct AppSessionContext {
  StrView SessionId;
  StrView ProjectKey;
  StrView WorkingDirectory;
  StrView TranscriptPath;
  StrView ArtifactDirectory;
  bool Interactive;
};

struct AppExtension {
  void* User;
  // `Hooks.User` is ignored; the hooks receive `User` like everything else.
  AgentHarnessHooks Hooks;
  // Main thread, no run active.
  void (*SessionChanged)(void* user, const AppSessionContext* session);
  // Event delivery thread, in agent order.
  void (*OnEvent)(void* user, const AgentHarnessEvent* event);
  void (*Shutdown)(void* user);
};

constexpr u32 APP_EXTENSION_CAPACITY = AGENT_HOOK_CHAIN_CAPACITY;

struct AppExtensions {
  AppExtension Items[APP_EXTENSION_CAPACITY];
  u32 Count;
  AgentHookChain Chain;
  bool ShutDown;
};

bool AppExtensions_Add(AppExtensions* extensions, const AppExtension* extension);

// `extensions` must outlive every harness configured with them.
AgentHarnessHooks AppExtensions_Hooks(AppExtensions* extensions);

void AppExtensions_SessionChanged(AppExtensions* extensions,
                                  const AppSessionContext* session);
void AppExtensions_OnEvent(AppExtensions* extensions, const AgentHarnessEvent* event);
void AppExtensions_Shutdown(AppExtensions* extensions);
