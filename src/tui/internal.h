#pragma once

#include "term/cell.h"
#include "tui/tui.h"
#include "ui/ui.h"

// Every function assumes the caller already holds `Tui::Lock` unless its name says otherwise.
namespace TuiInternal {

extern const char* kSpinner[10];
constexpr u64 kDoubleEscapeMs = 500;
constexpr u64 kSpinnerFrameMs = 60;
constexpr u64 kCopyNoticeMs = 1500;

const u8 SELECTION_BG = 238;
const u8 COPY_NOTICE_FG = 114;
// Context bar fill escalates at pi's footer thresholds: past 70% and 90%.
const u8 CONTEXT_FILL_FG = 250;
const u8 CONTEXT_WARN_FG = 214;
const u8 CONTEXT_FULL_FG = 196;
const u8 CONTEXT_TRACK_FG = 238;
constexpr u32 kContextBarCells = 8;

Style TuiSt(const Tui* t, u8 fg, u8 attr);
void Wake(Tui* t);
void WakeInput(Tui* t);

// Each stops at `cap - 1` bytes and keeps the buffer NUL-terminated.
void Put(char* buf, u32 cap, u32* len, const char* s);
void PutView(char* buf, u32 cap, u32* len, StrView s);
u32 PutViewCells(char* buf, u32 cap, u32* len, StrView s, u32 maxCells);
void PutNum(char* buf, u32 cap, u32* len, u64 v);
void PutScaled(char* buf, u32 cap, u32* len, u64 v);

StrView Truncate(StrView s, u32 maxBytes);
StrView TruncateTail(StrView s, u32 maxBytes);
u32 Utf8Count(StrView s);

struct TuiStatusBar {
  u32 Start;
  u32 FillEnd;
  u32 End;
  u8 FillFg;
  u32 SpinnerEnd;
};

void FormatStatus(Tui* t, char* left, u32 leftCap, u32* ll, char* right,
                  u32 rightCap, u32* rl, u16 cols, TuiStatusBar* bar);
void BuildStatusComponents(Tui* t, u16 statusY, u16 cols);
void BuildCwdComponent(Tui* t, u16 cwdY, u16 cols);
UiLayoutStyle Positioned(u16 x, u16 y, u16 width, u16 height);

void BuildDebugOverlayComponent(Tui* t, u16 cols, u16 rows, u64 nowMs);

void ClearCopyNotice(Tui* t);
void ArmCopyNotice(Tui* t, StrView text);
void AdvanceCopyNotice(Tui* t, u64 nowMs);

TuiDisplayEvent* QueueDisplayEvent(Tui* t, TuiDisplayEventKind kind);
// `startMs` is wall clock; 0 when no run is live.
void ResetRunStats(Tui* t, u64 startMs);
void SetDisplayPayload(TuiDisplayEvent* ev, StrView text);
void QueueText(Tui* t, StrView text);
void ResetDisplay(Tui* t);
void SetTransientText(TuiTransientText* field, StrView text);
void ResetToolUpdates(Tui* t);
TuiToolUpdateSlot* FindToolUpdate(Tui* t, StrView toolCallId);
TuiToolUpdateSlot* AcquireToolUpdate(Tui* t, StrView toolCallId, u16* outIndex);

void ResetScrollbar(Tui* t);
void ClearTranscriptSelection(Tui* t);
void ResetCommandPalette(Tui* t);

// Compact past this floor, then only once the arena doubles the live set measured
// at the last compaction, so copying stays amortized at any scrollback cap.
constexpr usize TUI_SESSION_COMPACT_FLOOR = (usize)2 << 20;

void ClearSessionScopedStringsLocked(Tui* t);
bool SessionArenaSwapSafeLocked(const Tui* t);
void RebuildTranscriptLocked(Tui* t, const Message* messages, u32 count,
                             bool allowSwap);


struct ModalComponentData {
  Tui* Controller;
  u16 X;
  u16 Y;
  u16 Width;
  u16 Height;
};

u16 ModalPanelHeight(const Tui* t, u16 rows);

void PaintModalNode(UiContext*, const UiNode* node, UiCanvas* canvas);
void ResetModalInput(Tui* t);
void FinishModal(Tui* t, TuiModalResult result);
void RequestStopLocked(Tui* t);
void HandleSelectKey(Tui* t, const Key& k);
void HandleTextKey(Tui* t, const Key& k);


struct TranscriptSelection {
  u32 FirstRow;
  u32 LastRow;
  u16 FirstCell;
  u16 LastCell;
};

void FocusComposer(Tui* t);
bool TranscriptPointsDiffer(const Tui* t);
bool NormalizeTranscriptSelection(const Tui* t, TranscriptSelection* selection);
bool TranscriptRangeForRow(const TranscriptSelection* selection, u32 row,
                           const DisplayRowLayout* layout, DisplayRowRange* out);
void RefreshTranscriptSelectionHash(Tui* t);
bool ValidateTranscriptSelection(Tui* t);
bool GetTranscriptSelection(Tui* t, TranscriptSelection* selection);
u16 TranscriptMouseCell(const Tui* t, u16 x);
bool TranscriptMousePoint(const Tui* t, u16 x, u16 y, u32* row, u16* cell);
void ScrollBy(Tui* t, i32 delta);
void UpdateMouseDrag(Tui* t);
bool AdvanceSelectionTick(Tui* t, u64 nowMs);
void QueueClipboard(Tui* t, StrView text);
bool QueueTranscriptClipboard(Tui* t);
bool PrepareClipboardOutput(Tui* t);
void WriteTermOutput(void* user, const char* data, u32 n);
bool DrainShutdownOutput(Tui* t, ScreenWriter write, void* user);

}
