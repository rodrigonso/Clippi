#pragma once

#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"

enum class ProcessFileLockStatus : u8 { Acquired, Busy, IoError };

struct ProcessFileLock {
  alignas(8) byte Storage[16];
  bool Held;
};

// Ownership is the open handle/fd, so process death releases the lock without stale-lock detection.
ProcessFileLockStatus ProcessFileLock_TryAcquire(Arena* scratch, StrView path,
                                                 ProcessFileLock* out);
// `path` must be the path acquired with, since that is what gets unlinked.
void ProcessFileLock_Release(ProcessFileLock* lock, StrView path);

// Never creates or removes the sentinel. On POSIX a lock held by the calling process reads as
// free, since F_GETLK reports only conflicting owners.
bool ProcessFileLock_IsHeld(Arena* scratch, StrView path);
