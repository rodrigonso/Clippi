#pragma once
#include "base/arena.h"
#include "base/str.h"

using LoopbackSocket = uintptr_t;
#define LOOPBACK_INVALID_SOCKET (~(LoopbackSocket)0)

struct LoopbackServer {
  LoopbackSocket Socket = LOOPBACK_INVALID_SOCKET;
  u16 Port;
};

struct LoopbackRequest {
  LoopbackSocket Socket = LOOPBACK_INVALID_SOCKET;
  StrView Target;
};

enum class LoopbackWait : u8 { Request, Aborted, TimedOut, Failed };

// Binds an OS-assigned port on 127.0.0.1 only.
bool LoopbackServer_Open(LoopbackServer* server);
bool LoopbackServer_OpenPort(LoopbackServer* server, u16 port);
void LoopbackServer_Close(LoopbackServer* server);

// `abort` is polled at least every 100 ms.
LoopbackWait LoopbackServer_Accept(LoopbackServer* server, Arena* arena,
                                   volatile i32* abort, u32 timeoutMs,
                                   LoopbackRequest* out);

void LoopbackRequest_Respond(LoopbackRequest* request, u32 status, StrView html);
