#pragma once

#include "base/thread.h"
#include "session/repository.h"

struct MemorySession;

struct MemoryRepository {
  Arena Mem;
  Mutex Lock;
  Array<MemorySession*> Sessions;
  u64 AppendCalls;
  // Tests assert the harness reads full history only once, at init.
  u64 ReadAllCalls;
  u64 FailOnAppendCall;
  bool Initialized;
};

void MemoryRepository_Init(MemoryRepository* repository);
void MemoryRepository_Destroy(MemoryRepository* repository);
SessionRepository MemoryRepository_AsRepository(MemoryRepository* repository);

// The affected writable log becomes permanently faulted.
void MemoryRepository_FailAfterAppends(MemoryRepository* repository,
                                       u32 successfulCalls);
void MemoryRepository_ClearFailure(MemoryRepository* repository);
