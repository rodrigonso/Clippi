#pragma once
#include "base/str.h"

enum class ToolViewKind : u8 { Unknown, Shell, Read, Grep, Find, Ls, Write, Edit, Ask, Remember };

struct ToolLabel {
  StrView Name;
  StrView Label;
};

struct ToolView {
  ToolViewKind Kind;
  StrView Marker;
  StrView Label;
  StrBuf Detail;
  StrBuf Outcome;
  u32 OutcomeSplit;
  u32 CountHint;
};

// An empty label falls back to the tool name.
void ToolView_Init(Arena* arena, ToolView* view, StrView name, StrView argsJson,
                   StrView label = {});
StrView ToolLabel_Find(const ToolLabel* labels, u32 count, StrView name);
void ToolView_Finish(ToolView* view, StrView result, bool isError);
