#pragma once

#include "base/process_file_lock.h"
#include "base/thread.h"
#include "session/repository.h"

struct JsonlLogHandle;

struct JsonlWriterClaim {
  DurableId SessionId;
  StrView ProjectKey;
  // Null between Acquire and the handle being constructed.
  JsonlLogHandle* Handle;
  bool Active;
};

struct JsonlRepository {
  // Lock guards shared handle/claim allocations; Init/Destroy are exclusive.
  Arena Mem;
  Mutex Lock;
  StrView Root;
  Array<JsonlWriterClaim> Claims;
  bool Initialized;
};

void JsonlRepository_Init(JsonlRepository* repository, StrView dataRoot);
void JsonlRepository_Destroy(JsonlRepository* repository);
SessionRepository JsonlRepository_AsRepository(JsonlRepository* repository);

StrView JsonlRepository_SessionPath(Arena* arena, JsonlRepository* repository,
                                    StrView projectKey, DurableId sessionId);

StrView JsonlRepository_SessionDir(Arena* arena, JsonlRepository* repository,
                                   StrView projectKey, DurableId sessionId);
