#pragma once
#include "base/array.h"
#include "base/str.h"
#include "json/json.h"
#include "json/schema.h"
#include "llm/types.h"

struct ToolResult {
  ContentArray Content;
  bool Terminate;
};

struct ToolArgs {
  const JsonValue* Root;
  StrView Json;
};

inline const JsonValue* ToolArgs_Get(const ToolArgs* args, const char* key) {
  return args ? Json_Get(args->Root, key) : nullptr;
}
inline StrView ToolArgs_GetStr(const ToolArgs* args, const char* key) {
  return args ? Json_GetStr(args->Root, key) : StrView{};
}

inline i64 ToolArgs_GetInt(const ToolArgs* args, const char* key, i64 fallback) {
  return args ? Json_GetInt(args->Root, key, fallback) : fallback;
}


struct ToolUpdateSink {
  void* User;
  bool* Accepting;
  void (*OnUpdate)(void* user, const ToolResult* partial);
};

inline void ToolUpdateSink_Emit(const ToolUpdateSink* sink,
                                const ToolResult* partial) {
  if (sink && sink->OnUpdate && (!sink->Accepting || *sink->Accepting))
    sink->OnUpdate(sink->User, partial);
}

// A `Sequential` tool forces the whole batch to run serially (as in pi).
enum class ToolExecMode : u8 { Default, Sequential };

// Zero-initialized must stay Sensitive so unknown tools require review.
enum class ToolPermissionEffect : u8 {
  Sensitive,
  Inspection,
  UserInteraction,
};

// `Action` must be the complete projection of the call; one too long to show
// in full is never sent to the automatic reviewer.
struct ToolPermissionInfo {
  StrView Action;
  StrView GrantKey;
  bool AutoAllow;
  bool ReviewIncomplete;
};

// Targets inside these roots are allowed by Auto mode without review.
struct ToolPermissionScope {
  StrView WorkingDirectory;
  const StrView* WritableRoots;
  u32 WritableRootCount;
};

struct Tool {
  StrView Name;
  StrView Label;
  StrView Description;
  // pi's `promptSnippet` / `promptGuidelines`; prompt-visible parity text.
  StrView PromptSnippet;
  const StrView* PromptGuidelines;
  u32 PromptGuidelineCount;
  StrView ParamsJsonSchema;
  ToolExecMode ExecMode;
  ToolPermissionEffect PermissionEffect;
  void* Ctx;
  bool (*PrepareArguments)(void* ctx, Arena* arena, JsonValue* input,
                           JsonValue** out, StrBuf* err);
  bool (*DescribePermission)(void* ctx, Arena* arena, const ToolArgs* args,
                             const ToolPermissionScope* scope,
                             ToolPermissionInfo* out, StrBuf* err);
  bool (*Execute)(void* ctx, Arena* arena, StrView toolCallId,
                  const ToolArgs* args, volatile i32* abort,
                  const ToolUpdateSink* updates, ToolResult* out, StrBuf* err);
};

struct ToolRuntime {
  const Tool* Definition;
  JsonSchema Schema;
};

template <usize N>
inline void Tool_SetPromptGuidelines(Tool* tool,
                                     const StrView (&guidelines)[N]) {
  tool->PromptGuidelines = guidelines;
  tool->PromptGuidelineCount = (u32)N;
}

inline ToolDef Tool_ToDef(const Tool& t) {
  ToolDef d;
  d.Name = t.Name;
  d.Description = t.Description;
  d.ParamsJsonSchema = t.ParamsJsonSchema;
  return d;
}
