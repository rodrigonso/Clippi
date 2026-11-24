#pragma once
#include "base/array.h"
#include "base/str.h"
#include "term/cell.h"
#include "tui/toolview.h"

struct Span {
  StrView Text;
  Style St;
  bool KeepLeading;
  // One-cell placeholder painted as DisplayRowLayout::LiveGlyph; selection and copy
  // keep the placeholder, so animation never invalidates them.
  bool Live;
};

struct SpanLine {
  Array<Span> Spans;
  Span Right;
  Span Continuation;
  Span Hidden;
  u64 EntryId;
  bool HasRight;
  bool HasContinuation;
  bool HasHiddenContent;
  bool IsEntryStart;
};

// Each decoded scalar occupies at least one cell, matching Screen_PutText.
struct DisplayRowLayout {
  const SpanLine* Line;
  StrView LiveGlyph;  // set by paint only; must occupy exactly one cell
  u16 Columns;
  u16 LeftLimit;
  u16 HiddenColumn;
  u16 RightColumn;
  u16 ContentEnd;
};

struct DisplayRowRun {
  StrView Text;
  Style St;
  u16 Column;
  u16 CellWidth;
};

// Zero-initialize before the first LineModel_NextDisplayRowRun call.
struct DisplayRowIterator {
  u32 SpanIndex;
  u16 Column;
};

struct DisplayRowRange {
  u16 Start;
  u16 End;
};

// Thinking is transient: at most one, always last.
enum class EntryKind : u8 { User, Assistant, Tool, Info, Error, Thinking, Footer };

struct TextEntry {
  StrBuf Content;
  StrView Timestamp;
};

struct ThinkingEntry {
  u32 ElapsedSeconds;
};

struct TurnStats {
  u64 DurationMs;
  u64 InputTokens;
  u64 OutputTokens;
  u32 Tools;
};

struct ToolEntry {
  StrView Id;
  ToolView View;
  StrBuf Preview;
  // Monotonic milliseconds; zero for calls replayed from a session log.
  u64 StartMs;
  u64 EndMs;
  u32 ElapsedSeconds;
  bool Done;
  bool Error;
  bool Live;
};

struct Entry {
  u64 Id;
  EntryKind Kind;
  bool Collapsed;
  bool Dirty;
  bool PreviewShown;
  bool Separator;
  u32 PreviewRows;
  u32 PreviousVisible;
  u64 RowStart;
  Array<SpanLine> Rows;
  usize RowsMark;
  usize RowsBytes;
  union {
    TextEntry Text;
    ToolEntry Tool;
    ThinkingEntry Thinking;
    TurnStats Footer;
  };
};

// All access, including const queries (which may update the derived layout),
// requires the controller's lock.
struct LineModel {
  Arena* Mem;
  Arena RowsMem;
  Arena LayoutMem;
  Array<Entry> Entries;
  Array<SpanLine> Lines;  // visible rows, in a ring starting at LinesHead
  u32 LinesHead;
  u32 FirstDirtyEntry;  // UINT32_MAX when clean
  u32 LayoutEntries;
  u32 LastVisibleEntry;
  u64 RowsBegin;  // uncropped coordinates are not renumbered during eviction
  u64 RowsEnd;
  u64 RowOffset;
  u64 ExpandedEntryId;
  u16 Width;
  u32 Cap;
  u32 AssistantStart;
  u32 AssistantRowOffset;
  usize LiveRowsBytes;
  u64 NextEntryId;
  u64 DisplayRevision;
  u32 LiveTools;
  bool PreviewBudgetDirty;
  bool TrimPending;
  bool HasAssistant;
  bool Color;
  bool Rounded;
  u8 Accent;
};

const u8 DEFAULT_ACCENT_FG = 214;

void LineModel_Init(LineModel* m, Arena* mem, u16 width, u32 cap,
                    bool color = true, bool rounded = true, u8 accent = DEFAULT_ACCENT_FG);
void LineModel_Release(LineModel* m);
void LineModel_Layout(LineModel* m);
void LineModel_SetWidth(LineModel* m, u16 width);
bool LineModel_SetRounded(LineModel* m, bool rounded);
u32 LineModel_Count(const LineModel* m);
bool LineModel_FitsAtWidth(LineModel* m, u16 width, u32 maxRows);
const SpanLine* LineModel_At(const LineModel* m, u32 i);
u64 LineModel_RowEntryId(const LineModel* m, u32 row);
u64 LineModel_DisplayRevision(const LineModel* m);
// True once anything but notices (Info/Error) has been added.
bool LineModel_HasConversation(const LineModel* m);
bool LineModel_FindEntryRows(const LineModel* m, u64 entryId, u32* firstRow, u32* rowCount);
bool LineModel_IsEntryCollapsed(const LineModel* m, u64 entryId);
bool LineModel_ToggleEntry(LineModel* m, u64 entryId);
bool LineModel_ToggleAllTools(LineModel* m);

DisplayRowLayout LineModel_LayoutDisplayRow(const SpanLine* line, u16 columns);
bool LineModel_NextDisplayRowRun(const DisplayRowLayout* layout, DisplayRowIterator* iterator,
                                 DisplayRowRun* run);
DisplayRowRange LineModel_NormalizeDisplayRowRange(const DisplayRowLayout* layout, u16 anchor,
                                                   u16 focus);
StrView LineModel_ExtractDisplayRow(Arena* arena, const DisplayRowLayout* layout, u16 anchor,
                                    u16 focus);
u64 LineModel_HashDisplayRow(const DisplayRowLayout* layout, u16 anchor, u16 focus);

void LineModel_BeginAssistant(LineModel* m);
void LineModel_AppendAssistant(LineModel* m, StrView delta);
void LineModel_EndAssistant(LineModel* m);
void LineModel_AddNotice(LineModel* m, EntryKind kind, StrView text);
void LineModel_AddUser(LineModel* m, StrView text, u64 timestampMs, bool steered = false);
void LineModel_AddFooter(LineModel* m, TurnStats stats);
bool LineModel_SetThinking(LineModel* m, u64 startMs, u64 nowMs);
bool LineModel_EndThinking(LineModel* m);
void LineModel_AddTool(LineModel* m, StrView toolId, StrView name, StrView argsJson,
                       u64 startMs = 0);
bool LineModel_UpdateTool(LineModel* m, StrView toolId, StrView preview);
bool LineModel_FinishTool(LineModel* m, StrView toolId, StrView result, StrView preview,
                          bool isError, u64 endMs = 0);
bool LineModel_TickTools(LineModel* m, u64 nowMs);
void LineModel_SettleTools(LineModel* m);
