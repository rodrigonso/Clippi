#pragma once
#include "base/str.h"

struct FileLockToken {
  void* Handle;
};

void FileLock_Init();

// Compares by exact string; `path` is copied.
FileLockToken FileLock_Acquire(StrView path);

void FileLock_Release(FileLockToken tok);
