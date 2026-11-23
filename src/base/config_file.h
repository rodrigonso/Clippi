#pragma once
#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"

enum class ConfigFileReadResult : u8 {
  Success,
  Missing,
  Failure,
};

// Successful content is null-terminated at [Length].
ConfigFileReadResult ConfigFile_ReadAll(Arena* arena, StrView path, StrView* out);

bool ConfigFile_EnsureParentDir(Arena* arena, StrView path);

// Resulting POSIX files are mode 0600.
bool ConfigFile_WriteAtomic(Arena* arena, StrView path, StrView content);
