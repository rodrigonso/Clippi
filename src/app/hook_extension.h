#pragma once

#include "app/extensions.h"
#include "app/hook_config.h"
#include "app/hook_gate.h"
#include "app/hook_runner.h"

struct HookExtension {
  HookConfig Config;
  HookRunner* Runner;
  // Survives `--no-session`: tool policy does not depend on a durable
  // transcript.
  HookGate* Gate;
};

// A bad file warns: a typo must not silently disable every hook.
void HookExtension_Init(HookExtension* hooks, Arena* arena, StrView path,
                        bool durable, bool interactive, volatile i32* abort);

AppExtension HookExtension_Make(HookExtension* hooks);
