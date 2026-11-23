#pragma once

#include "session/entry.h"

constexpr u32 SESSION_DISPLAY_TITLE_MAX_BYTES = 96;

// Keys name directories, so Windows compares case-insensitively; older
// sessions predate key folding and must still match.
inline bool SessionProjectKey_Eq(StrView a, StrView b) {
#if defined(_WIN32)
  if (a.Length != b.Length) return false;
  for (u32 i = 0; i < a.Length; i++) {
    char x = a.Data[i];
    char y = b.Data[i];
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
#else
  return StrView_Eq(a, b);
#endif
}

StrView SessionProjectKey(Arena* arena, StrView directory);

struct SessionInfo {
  DurableId SessionId;
  StrView ProjectKey;
  StrView WorkingDirectory;
  StrView ExplicitName;
  StrView DisplayTitle;
  StrView Label;
  DurableId ForkedFromSessionId;
  DurableId ForkedFromEntryId;

  StrView CurrentProvider;
  StrView CurrentModel;

  u64 CreatedAt;
  u64 StorageUpdatedAt;
  u64 LastActivityAt;
  u64 WallSpanMs;

  u64 InputTokens;
  u64 OutputTokens;
  u64 CacheReadTokens;
  u64 ReasoningTokens;
  u64 TotalTokens;

  u32 MessageCount;
  u32 UserMessageCount;
  u32 AssistantMessageCount;
  u32 ToolResultMessageCount;
  u32 ToolCallCount;
  u32 CompactionCount;

  bool Busy;
};

struct SessionMetadataBuildOptions {
  bool Busy;
  u64 NowMs;
};

StrView SessionMetadata_FlattenDisplayText(
    Arena* arena, StrView text,
    u32 maxBytes = SESSION_DISPLAY_TITLE_MAX_BYTES);

SessionResult SessionMetadata_Build(
    Arena* arena, const SessionRecord* records, u32 count,
    const SessionMetadataBuildOptions* options, SessionInfo* out);

void SessionInfo_SortByActivity(Array<SessionInfo>* items);
