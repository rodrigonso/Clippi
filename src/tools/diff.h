#pragma once
#include "base/arena.h"
#include "base/str.h"

struct DiffResult {
  StrView Display;
  StrView Patch;
  i32 FirstChangedLine;
};

DiffResult Diff_Make(Arena* arena, StrView path, StrView oldText, StrView newText, u32 context);
