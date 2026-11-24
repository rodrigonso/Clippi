#pragma once
#include "base/arena.h"
#include "base/thread.h"
#include "llm/types.h"
#include "term/keys.h"
#include "ui/lineedit.h"
#include "tui/linemodel.h"
#include "term/screen.h"
#include "tui/perf.h"
#include "tui/stream_reveal.h"
#include "term/term.h"
#include "ui/widgets.h"

// TuiAgentHooks callbacks run under the TUI lock (order `Tui::Lock -> implementation`)
// and must not call back into the TUI.
enum class TuiPermissionMode : u8 { Ask, Auto, Yolo };

struct TuiAgentHooks {
  void* User;
  bool (*Steer)(void* user, StrView text, StrView* outError);
  bool (*RequestCancel)(void* user);
  bool (*IsBusy)(void* user);
  TuiPermissionMode (*CyclePermission)(void* user);
};

enum class TuiState : u8 {
  Idle,
  Thinking,
  Streaming,
  Tool,
  Retry,
  PermissionReview,
};

enum class TuiModalKind : u8 { None, Select, Text };
enum class TuiModalResult : u8 { None, Pending, Confirmed, Back, Cancelled };

enum class TuiSubmissionKind : u8 {
  Prompt,
  Command,
  PermissionMode,
  PermissionAcknowledged,
};

struct TuiSubmission {
  TuiSubmissionKind Kind;
  StrView Text;
  TuiPermissionMode Permission;
};

enum class TuiDisplayEventKind : u8 {
  Text,
  MessageEnd,
  ToolStart,
  ToolUpdate,
  ToolEnd,
  User,
  Notice,
  Error,
  AgentStart,
  AgentEnd,
};

struct TuiDisplayEvent {
  TuiDisplayEventKind Kind;
  Arena* Mem;
  StrBuf Payload;
  StrView ToolCallId;
  StrView ToolName;
  StrView ToolArgsJson;
  StopReason Stop;
  Usage Use;
  u64 Timestamp;
  u16 ToolUpdateSlot;
  bool IsError;
  bool Steer;
};

struct TuiDisplayBatch {
  Arena Mem;
  TuiDisplayBatch* Next;
  u32 Head;
  u32 Count;
  TuiDisplayEvent Events[32];
};

struct TuiTransientText {
  Arena Mem;
  StrView Text;
};

struct TuiToolUpdateSlot {
  StrView ToolCallId;
  StrBuf Payload;
  bool Active;
  bool Queued;
};

enum { TUI_TOOL_UPDATE_SLOT_COUNT = 64 };

enum class TuiCommandInline : u8 {
  None,
  Debug,
  Stop,
  CancelTurn,
};

struct TuiCommandSpec {
  StrView Name;
  StrView Alias;
  StrView Description;
  TuiCommandInline Inline = TuiCommandInline::None;
  bool PrivateArguments = false;
};

// Strings backed by SessionMem. RebuildTranscriptLocked clears this whole struct on
// an arena swap, so any SessionMem-backed view on Tui belongs here.
struct TuiSessionStrings {
  StrView ModalTitle;
  StrView ModalCategory;
  StrView ModalPrompt;
  StrView* ModalOptions;    // array itself is also SessionMem-owned
  u32 ModalOptionCount;
};

struct TuiHydratedRun {
  bool Open;
  u64 StartMs;
  u64 LastMs;
  u32 Responses;
  TurnStats Stats;
};

struct TuiOpts {
  StrView ModelName = {};
  ModelRequestProfile Profile = {};
  TuiPermissionMode Permission = TuiPermissionMode::Auto;
  bool YoloAcknowledged = false;
  bool Color = true;
  bool Rounded = true;
  u8 Accent = 0;
  const TuiCommandSpec* Commands = nullptr;
  u32 CommandCount = 0;
};

struct Tui {
  Arena* Mem;
  // Terminal, screen, synchronization, cwd, branch and model survive Tui_ClearSession.
  Arena SessionMem;
  usize SessionMemBaseline;
  Arena CommandMem;
  Arena ModalMem;
  Arena ToolUpdateMem;
  Arena RevealMem;
  Arena ScreenMem;
  TuiAgentHooks Agent;
  Term Trm;  Screen Scr;
  UiContext Ui;
  UiTextInputState InputUi;
  LineModel Model;
  LineEdit Edit;
  LineEdit ModalEdit;
  KeyDecoder Dec;

  Mutex Lock;
  CondVar SubmitCv;
  CondVar ModalCv;
  CondVar RenderCv;
  Thread InputThread;
  Thread RenderThread;
  bool InputThreadUp;
  bool RenderThreadUp;
  bool SyncUp;
  bool SessionUp;
  bool CommandMemUp;
  bool ToolUpdateUp;
  bool ScreenUp;
  bool UiUp;
  bool Color;
  u8 Accent;

  TuiSubmission Queue[64];
  u32 QHead, QTail, QCount;

  bool Dirty;
  bool InputDirty;
  bool Stop;
  bool Quit;
  bool AgentRunning;
  volatile i32 AbortFlag;
  u64 LastEscapeMs;
  bool EscapeArmed;

  TuiState State;
  TuiTransientText StateTool;
  TuiTransientText RetryStatus;
  TuiTransientText OperationLabel;
  TuiTransientText OperationStatus;
  Usage Use;
  u32 ContextTokens;
  u32 Spinner;
  u64 SpinnerStartMs;
  bool MascotActive;
  u64 MascotSinceMs;
  u32 MascotWakeMs;  // 0 unless the empty-state mascot is animating
  StrView ModelName;
  ModelRequestProfile Profile;
  TuiPermissionMode Permission;
  bool YoloAcknowledged;
  bool YoloWarningActive;
  u32 YoloWarningVisibleMs;
  u64 YoloWarningLastFrameMs;
  TuiCommandSpec* Commands;
  u32 CommandCount;
  u32 CommandPaletteSelected;
  u32 CommandPaletteFirst;
  bool CommandPaletteDismissed;
  bool OperationActive;
  bool OperationYields;
  volatile i32 OperationAbortFlag;

  // Wall clock, so a rebuilt transcript derives the same numbers from the log.
  u64 RunStartMs;
  u32 RunResponses;
  TurnStats RunStats;
  u64 ThinkingSinceMs;
  TuiHydratedRun Hydrated;

  TuiModalKind ModalKind;
  TuiModalResult ModalResult;
  TextInputMode ModalInputMode;
  bool ModalInputTooLong;
  bool ModalTextReady;
  u32 ModalSelected;
  u32 ModalFirst;

  // Everything above owns its storage; these do not -- see TuiSessionStrings.
  TuiSessionStrings Sess;

  StrView Cwd;
  StrView Branch;
  char CwdStorage[4096];
  char BranchStorage[512];

  bool Follow;
  u32 Top;
  u32 LastTop;
  u16 LastPaneH;
  bool ScrollbarVisible;
  bool ScrollbarDragging;
  u16 LastScrollbarThumbTop;
  u16 LastScrollbarThumbH;
  u16 ScrollbarDragOffset;

  u64 HoveredEntryId;
  u64 PressedEntryId;
  u16 MouseX;
  u16 MouseY;
  u16 MouseDownX;
  u16 MouseDownY;
  bool HasMouse;
  bool MouseDragMoved;
  bool TranscriptDragging;

  // Coordinates in the LineModel revision captured when the drag began.
  u32 TranscriptAnchorRow;
  u32 TranscriptFocusRow;
  u16 TranscriptAnchorCell;
  u16 TranscriptFocusCell;
  u64 TranscriptSelectionRevision;
  u64 TranscriptSelectionHash;
  i32 SelectionEdgeDirection;
  u64 NextSelectionScrollMs;

  // Queued under Lock, then encoded by the render/shutdown path.
  StrBuf ClipboardRaw;
  StrBuf ClipboardOsc;

  u32 CopyNoticeChars;
  u64 CopyNoticeExpiryMs;  // 0 while armed but not yet stamped by a frame
  bool CopyNoticeActive;

  StreamReveal Reveal;
  TuiDisplayBatch* DisplayFirst;
  TuiDisplayBatch* DisplayLast;
  TuiDisplayBatch* DisplaySpare;
  TuiDisplayBatch* RevealBatch;
  TuiToolUpdateSlot ToolUpdates[TUI_TOOL_UPDATE_SLOT_COUNT];

  TuiPerf Perf;
};

bool Tui_Start(Tui* t, Arena* mem, TuiOpts opts);
void Tui_Stop(Tui* t);

void Tui_LoadStartupContext(Tui* t);
void Tui_SetStartupContext(Tui* t, StrView cwd, StrView branch);

void Tui_InitModel(Tui* t, Arena* mem, TuiOpts opts);

bool Tui_ClearSession(Tui* t);
void Tui_ReplaceTranscript(Tui* t, const Message* messages, u32 count,
                           bool follow = false);
// Idle only: nothing may be streaming. Start from an empty Tui_ReplaceTranscript;
// a non-empty one closes its last run.
void Tui_AppendTranscript(Tui* t, const Message* messages, u32 count);

// CompactSessionMemory re-checks under the lock and returns false if the moment passed.
bool Tui_SessionMemoryWantsCompaction(Tui* t);
bool Tui_CompactSessionMemory(Tui* t, const Message* messages, u32 count);

void Tui_FeedInput(Tui* t, const char* data, u32 n);

bool Tui_WaitForSubmit(Tui* t, TuiSubmission* out);

bool Tui_SubmissionPending(Tui* t, u32 waitMs);

// Commits the turn atomically against shutdown; false if shutdown already won.
bool Tui_AcceptPrompt(Tui* t, StrView text);

// Thread-safe; callers must do network, credential and other blocking work after return.
void Tui_CommandNotice(Tui* t, StrView text);
void Tui_CommandError(Tui* t, StrView text);
enum class TuiToggleAction : u8 { Disable, Enable, Toggle };
bool Tui_UpdateDebugOverlay(Tui* t, TuiToggleAction action);
bool Tui_UpdateRounded(Tui* t, TuiToggleAction action);
void Tui_SetCommands(Tui* t, const TuiCommandSpec* commands, u32 count);
void Tui_SetModelProfile(Tui* t, StrView modelName,
                         ModelRequestProfile profile);
void Tui_SetPermissionMode(Tui* t, TuiPermissionMode mode,
                          bool yoloAcknowledged);
void Tui_BeginOperation(Tui* t, StrView label, bool yieldsToInput = false);
void Tui_SetOperationStatus(Tui* t, StrView status);
bool Tui_EndOperation(Tui* t);
volatile i32* Tui_OperationAbort(Tui* t);

// Selection results remain available until the next modal begins; text is consumed.
bool Tui_BeginSelection(Tui* t, StrView title, const StrView* options, u32 optionCount,
                        u32 initialSelection = 0,
                        StrView category = SVL("Commands"));
bool Tui_BeginTextEntry(Tui* t, StrView title, StrView prompt, StrView initial = {},
                        StrView category = SVL("Commands"),
                        TextInputMode mode = TextInputMode::Plain);
TuiModalResult Tui_GetModalResult(Tui* t, u32* selected, StrView* text,
                                 Arena* arena = nullptr);

// Cond_Wait releases the TUI lock, so input and render threads stay live meanwhile.
TuiModalResult Tui_Select(Tui* t, StrView title, const StrView* options,
                         u32 optionCount, u32* selected,
                         u32 initialSelection = 0,
                         StrView category = SVL("Commands"));
TuiModalResult Tui_ReadText(Tui* t, Arena* arena, StrView title, StrView prompt,
                           StrView initial, StrView* out,
                           StrView category = SVL("Commands"),
                           TextInputMode mode = TextInputMode::Plain);
void Tui_OpenCommandPalette(Tui* t);

void Tui_TurnStarted(Tui* t);
void Tui_AppendAssistantText(Tui* t, StrView text);
void Tui_AssistantMessageEnded(Tui* t, StopReason stop, Usage use,
                               StrView errorMessage);
void Tui_ToolStarted(Tui* t, StrView callId, StrView name, StrView argumentsJson);
void Tui_ToolProgress(Tui* t, StrView callId, StrView preview);
void Tui_ToolEnded(Tui* t, StrView callId, StrView preview, bool isError);
void Tui_TurnEnded(Tui* t);
void Tui_RetryStarted(Tui* t, bool providerLevel, u32 delayMs, u32 attempt,
                      u32 maxAttempts);
void Tui_RetryEnded(Tui* t);
void Tui_PermissionReviewStarted(Tui* t, StrView toolName);
void Tui_PermissionReviewEnded(Tui* t);
void Tui_SetContextTokens(Tui* t, u32 tokens);
void Tui_AddContextTokens(Tui* t, u32 tokens);
void Tui_NoticeDroppedEvents(Tui* t, u32 count);

bool Tui_AdvanceStream(Tui* t, u64 nowMs);
bool Tui_HasPendingDisplay(const Tui* t);

void Tui_ComposeFrame(Tui* t, u16 cols, u16 rows, u64 nowMs = 0);
bool Tui_TestDrainShutdown(Tui* t, ScreenWriter write, void* user);

volatile i32* Tui_Abort(Tui* t);
