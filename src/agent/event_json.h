#pragma once

#include "agent/harness_event.h"
#include "base/str.h"

// Durable records go through SessionRecord_Encode verbatim so the stream
// matches the log. On failure `out` is left unchanged, never partial.
SessionResult AgentEventJson_Write(StrBuf* out, const AgentHarnessEvent* event);
void AgentEventJson_WriteResult(StrBuf* out, SessionResult result);
