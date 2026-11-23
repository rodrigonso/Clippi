#pragma once
#include "base/basics.h"

// Pointers into an arena stay stable for its lifetime (no relocation). Single-threaded.
struct Arena {
  byte* Base;
  usize Reserved;
  usize Committed;
  usize Used;
};

Arena ArenaCreate(usize reserveBytes = (usize)1 << 30);

void* ArenaAlloc(Arena* a, usize size, usize align);
void ArenaReset(Arena* a);
void ArenaResetSecure(Arena* a);
void ArenaRestore(Arena* a, usize pos);

// Not folded into ArenaReset: reset runs on hot paths where a decommit syscall costs more
// than the pages. Traps if `Used` is above `keepBytes`.
void ArenaTrim(Arena* a, usize keepBytes);

void ArenaRelease(Arena* a);
void ArenaReleaseSecure(Arena* a);

#define ArenaPushStruct(a, T) (T*)ArenaAlloc((a), sizeof(T), alignof(T))
#define ArenaPushArray(a, T, n) (T*)ArenaAlloc((a), sizeof(T) * (usize)(n), alignof(T))
