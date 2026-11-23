#pragma once

#include "base/array.h"
#include "base/str.h"
#include "llm/types.h"
#include "session/id.h"
#include "session/result.h"

// v2 is deliberately incompatible and pi-style: only the tree and model-visible
// state persist; active turns and queues are memory-only, as in pi.
constexpr u32 SESSION_SCHEMA_VERSION = 2;

enum class SessionRecordKind : u8 {
  SessionStarted,
  Message,
  Model,
  Thinking,
  Tools,
  Compaction,
  BranchLabel,
  SessionName,
  Leaf,
};

struct SessionStartedData {
  StrView WorkingDirectory;
  StrView ProjectKey;
  StrView Label;
  DurableId ForkedFromSessionId;
  DurableId ForkedFromEntryId;
};

struct MessageEntryData {
  DurableId ParentId;
  Message Value;
};

struct ModelEntryData {
  DurableId ParentId;
  StrView Provider;
  StrView Model;
};

struct ThinkingEntryData {
  DurableId ParentId;
  StrView Level;
};

struct ToolsEntryData {
  DurableId ParentId;
  Array<StrView> ToolNames;
};

struct CompactionEntryData {
  DurableId ParentId;
  // Inclusive span on ParentId's ancestry; may overlap earlier compactions.
  DurableId BaseEntryId;
  DurableId ThroughEntryId;
  DurableId SourceHeadId;
  Message Summary;
  bool HasUsage;
  Usage Use;
  // Sorted, deduplicated, cumulative; a modified path is never in ReadFiles.
  Array<StrView> ReadFiles;
  Array<StrView> ModifiedFiles;
};

struct BranchLabelEntryData {
  DurableId ParentId;
  StrView Label;
};

struct SessionNameEntryData {
  StrView Name;
};

// Only when the head moves where parent-chaining would not put it.
struct LeafEntryData {
  DurableId LeafId;
};

struct SessionRecord {
  u32 SchemaVersion;
  DurableId SessionId;
  u64 Seq;
  DurableId EntryId;
  SessionRecordKind Kind;
  u64 CreatedAt;

  union {
    SessionStartedData SessionStarted;
    MessageEntryData MessageEntry;
    ModelEntryData ModelEntry;
    ThinkingEntryData ThinkingEntry;
    ToolsEntryData ToolsEntry;
    CompactionEntryData CompactionEntry;
    BranchLabelEntryData BranchLabelEntry;
    SessionNameEntryData SessionNameEntry;
    LeafEntryData LeafEntry;
  };
};

bool SessionRecord_IsTreeNode(SessionRecordKind kind);
StrView SessionRecordKind_Name(SessionRecordKind kind);
DurableId SessionRecord_ParentId(const SessionRecord* record);
SessionResult SessionRecord_PrepareForkCopy(
    const SessionRecord* source, DurableId targetSessionId, u64 targetSeq,
    u64 createdAt, const Array<DurableId>* oldIds,
    const Array<DurableId>* newIds, SessionRecord* out);

SessionResult SessionRecord_Encode(StrBuf* out, const SessionRecord* record);

// Input must be NUL-terminated at text.Data[text.Length]; results owned by arena.
SessionResult SessionRecord_Decode(Arena* arena, StrView text, SessionRecord* out);

SessionResult SessionRecord_Clone(Arena* arena, const SessionRecord* source,
                                  SessionRecord* out);

Message Message_Clone(Arena* arena, const Message& source);

SessionResult SessionRecord_Validate(const SessionRecord* record);
