#pragma once
#include "base/arena.h"
#include "base/array.h"
#include "base/basics.h"
#include "base/str.h"

// Not in settings.json: AppSettings_Save drops keys it does not model.

constexpr u32 HOOK_DEFAULT_TIMEOUT_MS = 10000;

struct HookCommand {
  StrView Command;
  StrView Matcher;
  u32 TimeoutMs;
};

struct HookEventBinding {
  StrView Event;
  Array<HookCommand> Commands;
};

struct HookConfig {
  Array<HookEventBinding> Events;
  u32 CommandCount;
};

enum class HookConfigLoadResult : u8 {
  Success,
  Missing,
  Malformed,
  ReadFailure,
};

StrView HookConfig_DefaultPath(Arena* arena);

// Successful strings are owned by `arena`.
HookConfigLoadResult HookConfig_Load(Arena* arena, StrView path, HookConfig* out);
HookConfigLoadResult HookConfig_Parse(Arena* arena, StrView text, HookConfig* out);

bool HookMatcher_Accepts(StrView matcher, StrView subject);

u32 HookConfig_Select(const HookConfig* config, StrView event, StrView subject,
                      const HookCommand** out, u32 capacity);
