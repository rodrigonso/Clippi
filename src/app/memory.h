#pragma once

#include "agent/harness.h"
#include "app/extensions.h"
#include "app/wiki.h"

struct MemoryExtension {
  AgentHarness* Harness;
  StrView Store;
  bool Enabled;
  // Holds everything below; reset on every session change.
  Arena Mem;
  WikiLocation Location;
  StrView Root;
  StrView Pages;
  StrView WorkingDirectory;
  // The harness keeps a view of this array as its writable roots.
  StrView WritableRoots[1];
  WikiJournalEntry Session;
  u64 JournaledMs;
};

constexpr u64 MEMORY_REJOURNAL_MS = 24ull * 60 * 60 * 1000;

// `store` must outlive the extension.
void MemoryExtension_Init(MemoryExtension* memory, AgentHarness* harness,
                          StrView store, bool enabled);
AppExtension MemoryExtension_Make(MemoryExtension* memory);
void MemoryExtension_SetEnabled(MemoryExtension* memory, bool enabled);

// Register only when memory is on at startup: with it off the tool list, like
// the prompt, must match pi's. Refuses at call time if memory is off by then.
Tool RememberTool(MemoryExtension* memory);
