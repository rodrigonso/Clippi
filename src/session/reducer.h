#pragma once

#include "session/entry.h"

// Never rebuild on a per-turn path; Fold per append. `Records` mirrors the
// caller's array, which may relocate: refresh it (Fold does) before reading.
struct SessionReduction {
  const SessionRecord* Records;
  u32 RecordCount;
  DurableIdIndex Index;
  DurableId HeadId;
  bool HeadIsSet;

  StrView Provider;
  StrView Model;
  StrView ThinkingLevel;
  Array<StrView> ToolNames;
};

SessionResult SessionReducer_Reduce(Arena* arena, const SessionRecord* records, u32 count,
                                    SessionReduction* out);

// `pending`: earlier records of this batch, already checked but not folded.
SessionResult SessionReducer_ValidateAppend(const SessionReduction* reduction,
                                            const SessionRecord* candidate,
                                            const SessionRecord* pending = nullptr,
                                            u32 pendingCount = 0);

// `reduction` must already reflect records[0..count-2].
void SessionReducer_Fold(SessionReduction* reduction, const SessionRecord* records, u32 count);

// The array belongs to arena; its records remain borrowed from reduction.
SessionResult SessionReducer_Ancestry(const SessionReduction* reduction, Arena* arena,
                                      DurableId headId,
                                      Array<const SessionRecord*>* outBranch);

// Only the array belongs to arena; nested content is borrowed from reduction.
SessionResult SessionReducer_Context(Arena* arena, const SessionReduction* reduction,
                                     DurableId headId, Array<Message>* outMessages);

// Compactions appended after `throughId` still apply; unset means whole branch.
SessionResult SessionReducer_ContextThrough(Arena* arena, const SessionReduction* reduction,
                                            DurableId headId, DurableId throughId,
                                            Array<Message>* outMessages);

SessionResult SessionRecords_Validate(const SessionRecord* records, u32 count);
