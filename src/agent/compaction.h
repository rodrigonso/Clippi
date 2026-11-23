#pragma once

#include "llm/types.h"
#include "session/entry.h"

// Mirrors pi's `compaction` settings block.
struct AgentCompactionPolicy {
  bool Enabled;
  u32 ReserveTokens;
  u32 KeepRecentTokens;
};

constexpr u32 AGENT_COMPACTION_DEFAULT_RESERVE_TOKENS = 16384;
constexpr u32 AGENT_COMPACTION_DEFAULT_KEEP_RECENT_TOKENS = 20000;

// Deliberately coarse: `ReserveTokens` absorbs the error.
constexpr u32 AGENT_COMPACTION_BYTES_PER_TOKEN = 4;

inline AgentCompactionPolicy AgentCompactionPolicy_Default() {
  AgentCompactionPolicy policy = {};
  policy.Enabled = true;
  policy.ReserveTokens = AGENT_COMPACTION_DEFAULT_RESERVE_TOKENS;
  policy.KeepRecentTokens = AGENT_COMPACTION_DEFAULT_KEEP_RECENT_TOKENS;
  return policy;
}

bool AgentCompactionPolicy_IsValid(const AgentCompactionPolicy* policy);

// `Usage::Input` excludes cache hits, so they are added back.
inline u32 AgentCompaction_UsageTokens(const Usage* use) {
  return AddSaturated(AddSaturated(use->Input, use->CacheRead), use->Output);
}

u32 AgentCompaction_EstimateMessageTokens(const Message* message);
u32 AgentCompaction_EstimateContextTokens(const Message* messages, u32 count);

u32 AgentCompaction_ReportedContextTokens(const Message* messages, u32 count);

u32 AgentCompaction_ContextTokens(const Message* messages, u32 count);

// Usage predating the newest compaction measured the replaced context, so the
// result is 0 (unknown) until the next response.
u32 AgentCompaction_BranchContextTokens(const SessionRecord* const* branch, u32 count);

bool AgentCompaction_ShouldCompact(const AgentCompactionPolicy* policy,
                                   u32 contextWindow, u32 contextTokens);

// `Base` and `Through` are inclusive.
struct AgentCompactionCut {
  DurableId BaseEntryId;
  DurableId ThroughEntryId;
  bool HasRetainedTail;
};

// The retained tail only begins at a `Role::User` message so a tool call is
// never separated from its results. Unlike pi, no split-turn summary: with no
// fitting user boundary the whole branch is spanned.
bool AgentCompaction_SelectCutPoint(const SessionRecord* const* branch, u32 branchCount,
                                    u32 keepRecentTokens, AgentCompactionCut* out);

// Mirrors pi's `FileOperations`, including its blind spot for `shell` commands.
struct AgentFileOps {
  Array<StrView> Read;
  Array<StrView> Written;
  Array<StrView> Edited;
};

struct AgentFileLists {
  Array<StrView> ReadFiles;
  Array<StrView> ModifiedFiles;
};

// Also unions lists stored on Compaction records, which is what carries paths
// across successive compactions. Paths are copied into `arena`.
void AgentCompaction_CollectFileOps(Arena* arena, const SessionRecord* const* branch,
                                    u32 branchCount, DurableId throughEntryId,
                                    AgentFileOps* out);

void AgentCompaction_BuildFileLists(Arena* arena, const AgentFileOps* ops,
                                    AgentFileLists* out);

void AgentCompaction_AppendFileSections(StrBuf* out, const AgentFileLists* lists);
