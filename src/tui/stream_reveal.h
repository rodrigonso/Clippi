#pragma once
#include "base/arena.h"
#include "base/str.h"

// Pacing is display-only -- nothing on the agent thread ever waits on it.
struct StreamReveal {
  StrBuf Pending;
  u32 Offset;
  u64 LastTickMs;
  u64 DeadlineMs;
  u32 CreditMilliScalars;
};

constexpr u64 kRevealDeadlineMs = 120;

void StreamReveal_Init(StreamReveal* r, Arena* arena);
void StreamReveal_Push(StreamReveal* r, StrView text);
// No copy: the caller keeps text's buffer and backing Arena alive until the
// pending text has drained and this buffer is detached or replaced.
void StreamReveal_Adopt(StreamReveal* r, StrBuf text);
bool StreamReveal_HasPending(const StreamReveal* r);

// Arming the deadline is idempotent, so staying blocked cannot push it out.
// The returned view is valid until the next push or adopt.
StrView StreamReveal_NextFrame(StreamReveal* r, u64 nowMs, bool blocked);
