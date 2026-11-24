#include "app/dream.h"

#include <stdio.h>
#include <string.h>

#include "app/auth_flow.h"
#include "app/event_pump.h"
#include "app/model_cache.h"
#include "app/permission_reviewer.h"
#include "app/project_trust.h"
#include "app/runtime.h"
#include "base/config_file.h"
#include "base/file_io.h"
#include "base/process_file_lock.h"
#include "base/sysinfo.h"
#include "json/json.h"
#include "net/http.h"
#include "session/jsonl_repository.h"
#include "session/metadata.h"
#include "tools/exec.h"
#include "tools/filelock.h"
#include "tools/fs.h"
#include "tools/tools.h"
#include "prompts/prompts.h"

namespace {

void AppendClipped(StrBuf* out, StrView text, u32 maxBytes) {
  StrView clipped = StrView_ClampUtf8(text, maxBytes);
  StrBuf_Append(out, clipped);
  if (clipped.Length == text.Length) return;
  StrBuf_AppendCStr(out, "\n[... ");
  StrBuf_AppendU64(out, text.Length - clipped.Length);
  StrBuf_AppendCStr(out, " more bytes]");
}

// Dot names are skipped, hiding staged copies.
Array<StrView> SortedNames(Arena* arena, StrView dir, bool directories,
                           bool* listedOut = nullptr) {
  bool listed = false;
  Array<FsEntry> entries = Fs_ListDir(arena, dir, &listed);
  if (listedOut) *listedOut = listed;
  Array<StrView> names = Array_Make<StrView>(arena, entries.Count ? entries.Count : 1);
  for (u32 i = 0; i < entries.Count; i++) {
    const FsEntry& entry = entries.Data[i];
    if (entry.IsDir != directories || !entry.Name.Length || entry.Name.Data[0] == '.')
      continue;
    if (!directories && !StrView_EndsWith(entry.Name, SVL(".md"))) continue;
    Array_Push(&names, entry.Name);
  }
  Array_Sort(&names, [](StrView a, StrView b) { return StrView_Compare(a, b) < 0; });
  return names;
}

bool CopyPages(Arena* arena, StrView from, StrView to, u32* copied) {
  if (copied) *copied = 0;
  if (!Fs_MkdirP(arena, to)) return false;
  bool listed = false;
  Array<StrView> names = SortedNames(arena, from, false, &listed);
  // An existing source that cannot be listed is not an empty wiki.
  if (!listed && Fs_Exists(from)) return false;
  for (u32 i = 0; i < names.Count; i++) {
    StrView content = {};
    if (File_ReadAll(arena, Fs_Join(arena, from, names.Data[i]), &content) !=
            FileIoResult::Success ||
        File_WriteAll(arena, Fs_Join(arena, to, names.Data[i]), content) !=
            FileIoResult::Success)
      return false;
    if (copied) (*copied)++;
  }
  return true;
}

u32 CountPageChanges(Arena* arena, StrView before, StrView after) {
  Array<StrView> old = SortedNames(arena, before, false);
  Array<StrView> now = SortedNames(arena, after, false);
  u32 changed = 0;
  for (u32 i = 0; i < now.Count; i++) {
    StrView current = {};
    StrView previous = {};
    File_ReadAll(arena, Fs_Join(arena, after, now.Data[i]), &current);
    if (File_ReadAll(arena, Fs_Join(arena, before, now.Data[i]), &previous) !=
            FileIoResult::Success ||
        !StrView_Eq(current, previous))
      changed++;
  }
  for (u32 i = 0; i < old.Count; i++)
    if (!Fs_Exists(Fs_Join(arena, after, old.Data[i]))) changed++;
  return changed;
}

StrView Timestamp(Arena* arena, u64 nowMs) {
  struct tm t = {};
  char text[32] = "snapshot";
  if (Sys_LocalTime(nowMs, &t))
    snprintf(text, sizeof(text), "%04d%02d%02d-%02d%02d%02d", t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return StrView_Dup(arena, SV(text));
}

// Staged under a dot name: a partial snapshot must never be the newest one.
bool Snapshot(Arena* arena, StrView snapshots, StrView pages, u64 nowMs) {
  StrView base = Timestamp(arena, nowMs);
  StrView name = base;
  for (u32 n = 2; Fs_Exists(Fs_Join(arena, snapshots, name)); n++) {
    StrBuf numbered = StrBuf_Make(arena, base.Length + 8);
    StrBuf_Append(&numbered, base);
    StrBuf_AppendChar(&numbered, '-');
    StrBuf_AppendU64(&numbered, n);
    name = StrBuf_View(&numbered);
  }
  // The dream lock is held, so no other snapshot is in progress.
  bool listed = false;
  Array<FsEntry> entries = Fs_ListDir(arena, snapshots, &listed);
  for (u32 i = 0; i < entries.Count; i++)
    if (entries.Data[i].IsDir && entries.Data[i].Name.Length &&
        entries.Data[i].Name.Data[0] == '.')
      Fs_RemoveTree(arena, Fs_Join(arena, snapshots, entries.Data[i].Name));
  StrBuf hidden = StrBuf_Make(arena, name.Length + 2);
  StrBuf_AppendChar(&hidden, '.');
  StrBuf_Append(&hidden, name);
  StrView staging = Fs_Join(arena, snapshots, StrBuf_View(&hidden));
  if (!CopyPages(arena, pages, staging, nullptr) ||
      !File_Rename(arena, staging, Fs_Join(arena, snapshots, name))) {
    Fs_RemoveTree(arena, staging);
    return false;
  }
  Array<StrView> all = SortedNames(arena, snapshots, true);
  for (u32 i = 0; i + DREAM_SNAPSHOTS_KEPT < all.Count; i++)
    Fs_RemoveTree(arena, Fs_Join(arena, snapshots, all.Data[i]));
  return true;
}

void RenderMessage(StrBuf* out, const Message* message) {
  switch (message->Kind) {
    case Role::User: {
      StrBuf_AppendCStr(out, "## User\n\n");
      for (u32 i = 0; i < message->Content.Count; i++) {
        const Content& content = message->Content.Data[i];
        if (content.Kind == ContentKind::Text)
          AppendClipped(out, content.Text, DREAM_MESSAGE_MAX_BYTES);
        else if (content.Kind == ContentKind::Image)
          StrBuf_AppendCStr(out, "[image]");
        StrBuf_AppendChar(out, '\n');
      }
      break;
    }
    case Role::Assistant: {
      StrBuf_AppendCStr(out, "## Assistant\n\n");
      for (u32 i = 0; i < message->Content.Count; i++) {
        const Content& content = message->Content.Data[i];
        if (content.Kind == ContentKind::Text) {
          AppendClipped(out, content.Text, DREAM_MESSAGE_MAX_BYTES);
          StrBuf_AppendChar(out, '\n');
        } else if (content.Kind == ContentKind::ToolCall) {
          StrBuf_AppendCStr(out, "- tool call `");
          StrBuf_Append(out, content.ToolName);
          StrBuf_AppendCStr(out, "`: ");
          AppendClipped(out, content.ToolArgsJson, DREAM_TOOL_ARGS_MAX_BYTES);
          StrBuf_AppendChar(out, '\n');
        }
      }
      if (message->Stop == StopReason::Error && message->ErrorMessage.Length) {
        StrBuf_AppendCStr(out, "(the turn failed: ");
        AppendClipped(out, message->ErrorMessage, DREAM_TOOL_RESULT_MAX_BYTES);
        StrBuf_AppendCStr(out, ")\n");
      }
      break;
    }
    case Role::ToolResult: {
      StrBuf_AppendCStr(out, "## Tool result: ");
      StrBuf_Append(out, message->ToolName.Length ? message->ToolName : SVL("tool"));
      StrBuf_AppendCStr(out, message->IsError ? " (error)\n\n" : "\n\n");
      for (u32 i = 0; i < message->Content.Count; i++) {
        const Content& content = message->Content.Data[i];
        if (content.Kind != ContentKind::Text) continue;
        AppendClipped(out, content.Text,
                      message->IsError ? DREAM_MESSAGE_MAX_BYTES
                                       : DREAM_TOOL_RESULT_MAX_BYTES);
        StrBuf_AppendChar(out, '\n');
      }
      break;
    }
  }
  StrBuf_AppendChar(out, '\n');
}

bool HasReplayableContent(const Message* message) {
  for (u32 i = 0; i < message->Content.Count; i++) {
    ContentKind kind = message->Content.Data[i].Kind;
    if (kind != ContentKind::Thinking) return true;
  }
  return message->ErrorMessage.Length > 0;
}

}

StrView Dream_ExpandPrompt(Arena* arena, StrView prompt, const WikiLocation* location) {
  PromptVar vars[] = {
      {SVL("inbox"), Fs_ToPosix(arena, Fs_Join(arena, location->DreamDir, SVL("inbox")))},
      {SVL("pages"), Fs_ToPosix(arena, location->Pages)},
      {SVL("repo"), Fs_ToPosix(arena, location->Repository)},
      {SVL("page_limit"), SVL("8 KB")},
      {SVL("pinned_limit"), SVL("4 KB")},
  };
  return Prompt_Expand(arena, prompt, vars, (u32)(sizeof(vars) / sizeof(vars[0])));
}

void Dream_RenderDigest(Arena* arena, const SessionRecord* records, u32 count,
                        u64 afterSeq, StrView sessionId, StrView workingDirectory,
                        u32 maxBytes, DreamDigest* out) {
  *out = {};
  out->LastSeq = afterSeq;
  const u32 kHeaderReserve = 256 + sessionId.Length + workingDirectory.Length;
  StrBuf body = StrBuf_Make(arena, 4096);
  StrBuf piece = StrBuf_Make(arena, 1024);
  for (u32 i = 0; i < count; i++) {
    const SessionRecord& record = records[i];
    if (record.Seq <= afterSeq) continue;
    StrBuf_Clear(&piece);
    if (record.Kind == SessionRecordKind::Message &&
        HasReplayableContent(&record.MessageEntry.Value)) {
      RenderMessage(&piece, &record.MessageEntry.Value);
    } else if (record.Kind == SessionRecordKind::Compaction) {
      StrBuf_AppendCStr(&piece, "## Compaction summary\n\n");
      const Message& summary = record.CompactionEntry.Summary;
      for (u32 j = 0; j < summary.Content.Count; j++)
        if (summary.Content.Data[j].Kind == ContentKind::Text)
          AppendClipped(&piece, summary.Content.Data[j].Text, DREAM_MESSAGE_MAX_BYTES);
      StrBuf_AppendCStr(&piece, "\n\n");
    }
    if ((u64)kHeaderReserve + body.Length + piece.Length > maxBytes) {
      out->Truncated = true;
      break;
    }
    if (piece.Length) {
      if (!out->FirstSeq) out->FirstSeq = record.Seq;
      StrBuf_Append(&body, StrBuf_View(&piece));
    }
    out->LastSeq = record.Seq;
  }
  if (!body.Length) return;
  StrBuf markdown = StrBuf_Make(arena, body.Length + kHeaderReserve);
  StrBuf_AppendCStr(&markdown, "# Session ");
  StrBuf_Append(&markdown, sessionId);
  StrBuf_AppendCStr(&markdown, "\n\nWorking directory: ");
  StrBuf_Append(&markdown, workingDirectory);
  StrBuf_AppendCStr(&markdown, "\nEntries: ");
  StrBuf_AppendU64(&markdown, out->FirstSeq);
  StrBuf_AppendChar(&markdown, '-');
  StrBuf_AppendU64(&markdown, out->LastSeq);
  if (out->Truncated) StrBuf_AppendCStr(&markdown, " (continued by the next pass)");
  StrBuf_AppendCStr(&markdown, "\n\n");
  StrBuf_Append(&markdown, StrBuf_View(&body));
  out->Markdown = StrBuf_View(&markdown);
}

bool DreamState_Load(Arena* arena, StrView path, DreamState* out) {
  *out = {};
  out->Sessions = Array_Make<DreamSessionState>(arena, 16);
  StrView content = {};
  ConfigFileReadResult read = ConfigFile_ReadAll(arena, path, &content);
  if (read == ConfigFileReadResult::Missing) return true;
  if (read != ConfigFileReadResult::Success) return false;
  JsonValue* root = Json_Parse(arena, content);
  if (!Json_IsObject(root)) return false;
  i64 offset = Json_GetInt(root, "journalOffset", 0);
  out->JournalOffset = offset > 0 ? (u64)offset : 0;
  JsonValue* sessions = Json_Get(root, "sessions");
  for (u32 i = 0; Json_IsArray(sessions) && i < sessions->Count; i++) {
    JsonValue* item = sessions->Items[i];
    if (!Json_IsObject(item)) continue;
    DreamSessionState session = {};
    session.SessionId = Json_GetStr(item, "session");
    session.ProjectKey = Json_GetStr(item, "projectKey");
    session.WorkingDirectory = Json_GetStr(item, "cwd");
    i64 seq = Json_GetInt(item, "seq", 0);
    i64 seen = Json_GetInt(item, "seen", 0);
    session.Seq = seq > 0 ? (u64)seq : 0;
    session.SeenMs = seen > 0 ? (u64)seen : 0;
    if (session.SessionId.Length && session.ProjectKey.Length)
      Array_Push(&out->Sessions, session);
  }
  return true;
}

bool DreamState_Save(Arena* arena, StrView path, const DreamState* state) {
  StrBuf json = StrBuf_Make(arena, 256 + state->Sessions.Count * 160);
  JsonWriter w = {};
  JsonWrite_Begin(&w, &json);
  JsonWrite_U64(&w, "journalOffset", state->JournalOffset);
  JsonWrite_Key(&w, "sessions");
  StrBuf_AppendChar(&json, '[');
  for (u32 i = 0; i < state->Sessions.Count; i++) {
    const DreamSessionState& session = state->Sessions.Data[i];
    if (i) StrBuf_AppendChar(&json, ',');
    JsonWriter item = {};
    JsonWrite_Begin(&item, &json);
    JsonWrite_String(&item, "session", session.SessionId);
    JsonWrite_String(&item, "projectKey", session.ProjectKey);
    JsonWrite_String(&item, "cwd", session.WorkingDirectory);
    JsonWrite_U64(&item, "seq", session.Seq);
    JsonWrite_U64(&item, "seen", session.SeenMs);
    JsonWrite_End(&item);
  }
  StrBuf_AppendChar(&json, ']');
  JsonWrite_End(&w);
  return ConfigFile_WriteAtomic(arena, path, StrBuf_View(&json));
}

namespace {

const StrView kPhaseNames[] = {SVL("preparing"), SVL("dreaming"), SVL("finished")};
const StrView kOutcomeNames[] = {SVL("dreamed"), SVL("nothing"), SVL("busy"),
                                 SVL("failed")};
constexpr u32 kProgressMessageMaxBytes = 1024;

template <typename T, usize N>
bool ParseName(StrView text, const StrView (&names)[N], T* out) {
  for (u32 i = 0; i < N; i++) {
    if (!StrView_Eq(text, names[i])) continue;
    *out = (T)i;
    return true;
  }
  return false;
}

}

bool DreamProgress_Load(Arena* arena, StrView path, DreamProgress* out) {
  *out = {};
  StrView content = {};
  if (ConfigFile_ReadAll(arena, path, &content) != ConfigFileReadResult::Success)
    return false;
  JsonValue* root = Json_Parse(arena, content);
  if (!Json_IsObject(root) ||
      !ParseName(Json_GetStr(root, "phase"), kPhaseNames, &out->Phase))
    return false;
  i64 started = Json_GetInt(root, "started", 0);
  out->StartedMs = started > 0 ? (u64)started : 0;
  out->SessionId = Json_GetStr(root, "session");
  out->ProjectKey = Json_GetStr(root, "projectKey");
  if (out->Phase == DreamPhase::Finished &&
      !ParseName(Json_GetStr(root, "outcome"), kOutcomeNames, &out->Outcome))
    return false;
  i64 digests = Json_GetInt(root, "digests", 0);
  i64 changed = Json_GetInt(root, "pagesChanged", 0);
  out->Digests = digests > 0 ? (u32)digests : 0;
  out->PagesChanged = changed > 0 ? (u32)changed : 0;
  out->Message = Json_GetStr(root, "message");
  return true;
}

bool DreamProgress_Save(Arena* arena, StrView path, const DreamProgress* progress) {
  StrBuf json = StrBuf_Make(arena, 512);
  JsonWriter w = {};
  JsonWrite_Begin(&w, &json);
  JsonWrite_U64(&w, "started", progress->StartedMs);
  JsonWrite_String(&w, "phase", kPhaseNames[(u32)progress->Phase]);
  if (progress->SessionId.Length) {
    JsonWrite_String(&w, "session", progress->SessionId);
    JsonWrite_String(&w, "projectKey", progress->ProjectKey);
  }
  if (progress->Phase == DreamPhase::Finished) {
    JsonWrite_String(&w, "outcome", kOutcomeNames[(u32)progress->Outcome]);
    JsonWrite_U64(&w, "digests", progress->Digests);
    JsonWrite_U64(&w, "pagesChanged", progress->PagesChanged);
    JsonWrite_String(&w, "message",
                     StrView_ClampUtf8(progress->Message, kProgressMessageMaxBytes));
  }
  JsonWrite_End(&w);
  return ConfigFile_WriteAtomic(arena, path, StrBuf_View(&json));
}

namespace {

struct DreamPaths {
  StrView Inbox;
  StrView Snapshots;
  StrView State;
  StrView Progress;
  StrView Lock;
  StrView Report;
};

DreamPaths PathsFor(Arena* arena, const WikiLocation* location) {
  DreamPaths paths = {};
  paths.Inbox = Fs_Join(arena, location->DreamDir, SVL("inbox"));
  paths.Snapshots = Fs_Join(arena, location->DreamDir, SVL("snapshots"));
  paths.State = Fs_Join(arena, location->DreamDir, SVL("state.json"));
  paths.Progress = Fs_Join(arena, location->DreamDir, SVL("progress.json"));
  paths.Lock = Fs_Join(arena, location->DreamDir, SVL("lock"));
  paths.Report = Fs_Join(arena, location->DreamDir, SVL("last-report.md"));
  return paths;
}

struct DreamSandbox {
  StrView Root;
  StrView Pages;
};

void SandboxGate(void* user, Arena* arena, StrView toolName, StrView,
                 const ToolArgs* args, AgentToolGateDecision* out) {
  DreamSandbox* sandbox = (DreamSandbox*)user;
  if (!StrView_Eq(toolName, SVL("write")) && !StrView_Eq(toolName, SVL("edit")))
    return;
  StrView target = Fs_MutationTarget(arena, toolName, args, sandbox->Root);
  if (target.Length && Fs_PathContained(sandbox->Pages, target) &&
      !Fs_PathEquals(target, sandbox->Pages))
    return;
  out->Block = true;
  out->Reason = SVL("Dreaming may only write wiki pages in the pages folder.");
}

struct PendingSession {
  u32 StateIndex;
  u64 NewSeq;
  StrView InboxName;
  DreamDigest Digest;
};

bool JournaledInRange(const Array<WikiJournalEntry>* journal, StrView sessionId) {
  for (u32 i = 0; i < journal->Count; i++)
    if (journal->Data[i].Kind == WikiJournalKind::Session &&
        StrView_Eq(journal->Data[i].SessionId, sessionId))
      return true;
  return false;
}

void Fail(DreamResult* out, DreamOutcome outcome, StrView message) {
  out->Outcome = outcome;
  out->Message = message;
}

StrView FirstMessage(Arena* arena, const Array<PendingSession>* pending,
                     const DreamState* state, const Array<StrView>* pagesWritten,
                     const Array<StrView>* notes, const Wiki* wiki,
                     const Array<WikiIssue>* issues) {
  StrBuf message = StrBuf_Make(arena, 4096);
  StrBuf_AppendCStr(&message, "Dream over the new activity in this repository.\n\n");
  StrBuf_AppendCStr(&message, "Session digests:\n");
  u32 digests = 0;
  for (u32 i = 0; i < pending->Count; i++) {
    const PendingSession& session = pending->Data[i];
    if (!session.InboxName.Length) continue;
    const DreamSessionState& source = state->Sessions.Data[session.StateIndex];
    StrBuf_AppendCStr(&message, "- .dream/inbox/");
    StrBuf_Append(&message, session.InboxName);
    StrBuf_AppendCStr(&message, " (");
    StrBuf_Append(&message, source.WorkingDirectory);
    StrBuf_AppendCStr(&message, ", entries ");
    StrBuf_AppendU64(&message, session.Digest.FirstSeq);
    StrBuf_AppendChar(&message, '-');
    StrBuf_AppendU64(&message, session.Digest.LastSeq);
    StrBuf_AppendCStr(&message, ")\n");
    digests++;
  }
  if (!digests) StrBuf_AppendCStr(&message, "- none: no new session activity\n");

  StrBuf_AppendCStr(&message, "\nPages sessions wrote since the last dream: ");
  for (u32 i = 0; i < pagesWritten->Count; i++) {
    StrBuf_AppendCStr(&message, i ? ", [[" : "[[");
    StrBuf_Append(&message, pagesWritten->Data[i]);
    StrBuf_AppendCStr(&message, "]]");
  }
  if (!pagesWritten->Count) StrBuf_AppendCStr(&message, "none");

  StrBuf_AppendCStr(&message, "\n\nRemembered notes:\n");
  for (u32 i = 0; i < notes->Count; i++) {
    StrBuf_AppendCStr(&message, "- ");
    for (u32 c = 0; c < notes->Data[i].Length; c++) {
      char ch = notes->Data[i].Data[c];
      StrBuf_AppendChar(&message, ch == '\n' || ch == '\r' ? ' ' : ch);
    }
    StrBuf_AppendChar(&message, '\n');
  }
  if (!notes->Count) StrBuf_AppendCStr(&message, "- none\n");

  StrBuf_AppendCStr(&message, "\nLint findings:\n");
  if (issues->Count) Wiki_AppendIssues(&message, issues);
  else StrBuf_AppendCStr(&message, "- none\n");

  StrBuf_AppendCStr(&message, "\nWiki: ");
  StrBuf_AppendU64(&message, wiki->Pages.Count);
  StrBuf_AppendCStr(&message, " pages, ");
  StrBuf_AppendU64(&message, wiki->TotalBytes);
  StrBuf_AppendCStr(&message, " of ");
  StrBuf_AppendU64(&message, WIKI_TOTAL_MAX_BYTES);
  StrBuf_AppendCStr(&message, " bytes; pinned ");
  StrBuf_AppendU64(&message, wiki->PinnedBytes);
  StrBuf_AppendCStr(&message, " of ");
  StrBuf_AppendU64(&message, WIKI_PINNED_MAX_BYTES);
  StrBuf_AppendCStr(&message, " bytes.\n\nCurrent catalog:\n");
  if (wiki->Pages.Count) Wiki_AppendCatalog(arena, &message, wiki, nullptr);
  else StrBuf_AppendCStr(&message, "(empty)\n");
  return StrBuf_View(&message);
}

StrView LastAssistantText(Arena* arena, AgentHarness* harness) {
  AgentTranscriptSnapshot snapshot = {};
  if (!SessionResult_IsOk(AgentHarness_SnapshotTranscript(arena, harness, 8, &snapshot)))
    return {};
  for (u32 i = snapshot.Transcript.Count; i > 0; i--) {
    const Message& message = snapshot.Transcript.Data[i - 1];
    if (message.Kind != Role::Assistant) continue;
    StrBuf text = StrBuf_Make(arena, 256);
    for (u32 j = 0; j < message.Content.Count; j++)
      if (message.Content.Data[j].Kind == ContentKind::Text)
        StrBuf_Append(&text, message.Content.Data[j].Text);
    if (text.Length) return StrView_Trim(StrBuf_View(&text));
  }
  return {};
}

bool RunAgent(Arena* arena, const DreamRun* run, const DreamPaths* paths,
              StrView firstMessage, DreamProgress* progress, DreamResult* out,
              StrView* error) {
  const WikiLocation* location = &run->Location;
  SessionCreateOptions create = {};
  create.ProjectKey = SessionProjectKey(arena, location->Root);
  create.WorkingDirectory = location->Root;
  create.Label = SVL("dream");
  SessionLog log = {};
  SessionInfo info = {};
  SessionResult created =
      run->Sessions->Create(run->Sessions->Ctx, arena, &create, &log, &info);
  if (!SessionResult_IsOk(created)) {
    *error = created.Message;
    return false;
  }
  out->SessionId = info.SessionId;
  progress->Phase = DreamPhase::Dreaming;
  progress->SessionId = StrView_Dup(arena, DurableId_View(&out->SessionId));
  progress->ProjectKey = create.ProjectKey;
  DreamProgress_Save(arena, paths->Progress, progress);

  DreamSandbox sandbox = {};
  sandbox.Root = Fs_PermissionPath(arena, location->Root, location->Root);
  sandbox.Pages = Fs_PermissionPath(arena, location->Root, location->Pages);

  Tool tools[] = {ReadTool(), WriteTool(), EditTool(), LsTool(), FindTool(), GrepTool()};
  AgentHarnessConfig config = {};
  config.SystemPrompt = run->SystemPrompt;
  config.ModelInfo = run->ModelInfo;
  config.Tools = tools;
  config.ToolCount = sizeof(tools) / sizeof(tools[0]);
  config.ProviderImpl = run->ProviderImpl;
  config.Stream.Profile = run->Profile;
  config.ToolExec = ToolExecution::Parallel;
  config.Retry = run->Retry;
  config.TurnLimit = run->TurnLimit;
  config.Compaction = run->Compaction;
  config.PermissionMode = AgentPermissionMode::Auto;
  config.PermissionReviewer = run->Reviewer;
  config.PermissionReviewerModel = run->ReviewerModel;
  config.WorkingDirectory = location->Root;
  config.Hooks.User = &sandbox;
  config.Hooks.BeforeTool = SandboxGate;
  config.Events = run->Events;
  config.AuthUser = run->AuthUser;
  config.ResolveAuth = run->ResolveAuth;

  AgentHarness harness = {};
  SessionResult result = AgentHarness_Init(&harness, log, &config);
  if (!SessionResult_IsOk(result)) {
    *error = result.Message;
    return false;
  }
  result = AgentHarness_SetModel(&harness, run->ModelInfo, run->ProviderImpl,
                                 run->Profile);
  if (SessionResult_IsOk(result) && run->ResolveAuth &&
      !ResolvePromptAuth(&harness, &harness.AbortFlag))
    result = SessionResult_Error(SessionStatus::InvalidState,
                                 SVL("no credentials are configured for the dream model"));
  if (SessionResult_IsOk(result)) result = RunHarnessPrompt(&harness, firstMessage);

  for (u32 round = 0; SessionResult_IsOk(result) && round < DREAM_LINT_FOLLOW_UPS;
       round++) {
    Wiki wiki = {};
    Wiki_Scan(arena, location->Pages, &wiki);
    Array<WikiIssue> issues = Array_Make<WikiIssue>(arena, 16);
    Wiki_Lint(arena, &wiki, &issues);
    if (!Wiki_ErrorCount(&issues)) break;
    StrBuf issueList = StrBuf_Make(arena, 1024);
    Wiki_AppendIssues(&issueList, &issues);
    PromptVar vars[] = {{SVL("issues"), StrBuf_View(&issueList)}};
    result = RunHarnessPrompt(
        &harness, Prompt_Expand(arena, PROMPT_DREAM_LINT_FOLLOWUP, vars, 1));
  }

  out->Message = LastAssistantText(arena, &harness);
  SessionResult closed = AgentHarness_Close(&harness);
  if (SessionResult_IsOk(result)) result = closed;
  if (!SessionResult_IsOk(result)) {
    *error = StrView_Dup(arena, result.Message);
    return false;
  }
  return true;
}

void ExecuteLocked(Arena* arena, const DreamRun* run, const DreamPaths* paths,
                   DreamProgress* progress, DreamResult* out) {
  const WikiLocation* location = &run->Location;
  DreamState state = {};
  if (!DreamState_Load(arena, paths->State, &state)) {
    Fail(out, DreamOutcome::Failed, SVL("the dream state file is unreadable"));
    return;
  }

  Array<WikiJournalEntry> journal = Array_Make<WikiJournalEntry>(arena, 32);
  u64 journalEnd = state.JournalOffset;
  WikiJournal_Read(arena, location->Journal, state.JournalOffset, &journal, &journalEnd);
  Array<StrView> pagesWritten = Array_Make<StrView>(arena, 8);
  Array<StrView> notes = Array_Make<StrView>(arena, 8);
  for (u32 i = 0; i < journal.Count; i++) {
    const WikiJournalEntry& entry = journal.Data[i];
    if (entry.Kind == WikiJournalKind::Note && entry.Text.Length)
      Array_Push(&notes, StrView_ClampUtf8(entry.Text, WIKI_NOTE_MAX_BYTES));
    if (entry.Kind == WikiJournalKind::Page && Wiki_ValidSlug(entry.Slug)) {
      bool seen = false;
      for (u32 j = 0; j < pagesWritten.Count && !seen; j++)
        seen = StrView_Eq(pagesWritten.Data[j], entry.Slug);
      if (!seen) Array_Push(&pagesWritten, entry.Slug);
    }
    if (entry.Kind != WikiJournalKind::Session || !entry.SessionId.Length ||
        !entry.ProjectKey.Length)
      continue;
    DreamSessionState* known = nullptr;
    for (u32 j = 0; j < state.Sessions.Count && !known; j++)
      if (StrView_Eq(state.Sessions.Data[j].SessionId, entry.SessionId))
        known = &state.Sessions.Data[j];
    if (!known) {
      Array_Push(&state.Sessions, DreamSessionState{});
      known = &state.Sessions.Data[state.Sessions.Count - 1];
      known->SessionId = entry.SessionId;
    }
    known->ProjectKey = entry.ProjectKey;
    known->WorkingDirectory = entry.WorkingDirectory;
    if (entry.TimeMs > known->SeenMs) known->SeenMs = entry.TimeMs;
  }

  u32 kept = 0;
  for (u32 i = 0; i < state.Sessions.Count; i++)
    if (JournaledInRange(&journal, state.Sessions.Data[i].SessionId) ||
        state.Sessions.Data[i].SeenMs + DREAM_FORGET_MS >= run->NowMs)
      state.Sessions.Data[kept++] = state.Sessions.Data[i];
  state.Sessions.Count = kept;

  Fs_RemoveTree(arena, paths->Inbox);
  Fs_MkdirP(arena, paths->Inbox);
  Array<PendingSession> pending = Array_Make<PendingSession>(arena, state.Sessions.Count + 1);
  u64 budget = DREAM_INBOX_MAX_BYTES;
  u64 share = (u64)run->Profile.ContextWindow * AGENT_COMPACTION_BYTES_PER_TOKEN / 3;
  if (share && share < budget) budget = share < 16384 ? 16384 : share;
  for (u32 i = 0; i < state.Sessions.Count && budget > 4096; i++) {
    const DreamSessionState& source = state.Sessions.Data[i];
    if (source.Seq && source.SeenMs + DREAM_RECHECK_MS < run->NowMs &&
        !JournaledInRange(&journal, source.SessionId))
      continue;
    DurableId sessionId = {};
    if (!DurableId_Parse(source.SessionId, &sessionId)) continue;
    Arena scratch = ArenaCreate();
    SessionOpenOptions open = {};
    open.SessionId = sessionId;
    open.ProjectKey = source.ProjectKey;
    open.Writable = false;
    SessionLog log = {};
    SessionInfo info = {};
    Array<SessionRecord> records = {};
    bool read = SessionResult_IsOk(
                    run->Sources->Open(run->Sources->Ctx, &scratch, &open, &log, &info)) &&
                SessionResult_IsOk(SessionLog_ReadAll(&log, &scratch, &records));
    SessionLog_Close(&log);
    if (read) {
      PendingSession session = {};
      session.StateIndex = i;
      Dream_RenderDigest(arena, records.Data, records.Count, source.Seq,
                         source.SessionId, source.WorkingDirectory, (u32)budget,
                         &session.Digest);
      session.NewSeq = session.Digest.LastSeq;
      if (session.Digest.Markdown.Length) {
        StrBuf name = StrBuf_Make(arena, source.SessionId.Length + 4);
        StrBuf_Append(&name, source.SessionId);
        StrBuf_AppendCStr(&name, ".md");
        if (File_WriteAll(arena, Fs_Join(arena, paths->Inbox, StrBuf_View(&name)),
                          session.Digest.Markdown) == FileIoResult::Success) {
          session.InboxName = StrBuf_View(&name);
          budget -= session.Digest.Markdown.Length;
          out->Digests++;
        } else {
          session.NewSeq = source.Seq;
        }
      }
      if (session.NewSeq != source.Seq || session.InboxName.Length)
        Array_Push(&pending, session);
    }
    ArenaRelease(&scratch);
  }

  Wiki wiki = {};
  Wiki_Scan(arena, location->Pages, &wiki);
  Array<WikiIssue> issues = Array_Make<WikiIssue>(arena, 16);
  Wiki_Lint(arena, &wiki, &issues);

  bool commit = true;
  if (!out->Digests && !pagesWritten.Count && !notes.Count && !Wiki_ErrorCount(&issues)) {
    Fail(out, DreamOutcome::NothingToDo, SVL("Nothing new to dream about."));
  } else if (!Snapshot(arena, paths->Snapshots, location->Pages, run->NowMs)) {
    Fail(out, DreamOutcome::Failed, SVL("the wiki could not be snapshotted"));
    commit = false;
  } else {
    StrView previous = Sys_CurrentDir(arena);
    StrView error = {};
    bool ran = false;
    if (!Sys_SetCurrentDir(location->Root)) {
      error = SVL("could not enter the wiki folder");
    } else {
      ran = RunAgent(arena, run, paths,
                     FirstMessage(arena, &pending, &state, &pagesWritten, &notes, &wiki,
                                  &issues),
                     progress, out, &error);
      Sys_SetCurrentDir(previous);
    }
    Array<StrView> snapshots = SortedNames(arena, paths->Snapshots, true);
    if (snapshots.Count)
      out->PagesChanged = CountPageChanges(
          arena, Fs_Join(arena, paths->Snapshots, snapshots.Data[snapshots.Count - 1]),
          location->Pages);
    WikiJournalEntry entry = {};
    entry.Kind = WikiJournalKind::Dream;
    entry.TimeMs = run->NowMs;
    entry.SessionId = DurableId_IsSet(out->SessionId)
                          ? StrView_Dup(arena, DurableId_View(&out->SessionId))
                          : StrView{};
    entry.Status = ran ? SVL("ok") : SVL("failed");
    entry.PagesChanged = out->PagesChanged;
    WikiJournal_Append(arena, location->Journal, &entry);
    if (ran) {
      out->Outcome = DreamOutcome::Dreamed;
      if (out->Message.Length)
        File_WriteAll(arena, paths->Report, out->Message);
    } else {
      Fail(out, DreamOutcome::Failed, error.Length ? error : SVL("the dream failed"));
      commit = false;
    }
  }

  if (commit) {
    for (u32 i = 0; i < pending.Count; i++) {
      DreamSessionState* session = &state.Sessions.Data[pending.Data[i].StateIndex];
      if (pending.Data[i].NewSeq > session->Seq && run->NowMs > session->SeenMs)
        session->SeenMs = run->NowMs;
      session->Seq = pending.Data[i].NewSeq;
    }
    state.JournalOffset = journalEnd;
    if (!DreamState_Save(arena, paths->State, &state) &&
        out->Outcome != DreamOutcome::Failed)
      Fail(out, DreamOutcome::Failed, SVL("the dream state could not be saved"));
  }
  Fs_RemoveTree(arena, paths->Inbox);
}

}

void Dream_Execute(Arena* arena, const DreamRun* run, DreamResult* out) {
  *out = {};
  if (!arena || !run || !run->Sources || !run->Sessions || !run->Location.Root.Length) {
    Fail(out, DreamOutcome::Failed, SVL("the dream is misconfigured"));
    return;
  }
  DreamPaths paths = PathsFor(arena, &run->Location);
  if (!Fs_MkdirP(arena, run->Location.Pages) ||
      !Fs_MkdirP(arena, run->Location.DreamDir)) {
    Fail(out, DreamOutcome::Failed, SVL("the wiki folders could not be created"));
    return;
  }
  ProcessFileLock lock = {};
  ProcessFileLockStatus locked = ProcessFileLock_TryAcquire(arena, paths.Lock, &lock);
  if (locked == ProcessFileLockStatus::Busy) {
    Fail(out, DreamOutcome::Busy,
         SVL("Another dream is already running for this repository."));
    return;
  }
  if (locked != ProcessFileLockStatus::Acquired) {
    Fail(out, DreamOutcome::Failed, SVL("the dream lock could not be taken"));
    return;
  }
  DreamProgress progress = {};
  progress.StartedMs = run->NowMs;
  progress.Phase = DreamPhase::Preparing;
  DreamProgress_Save(arena, paths.Progress, &progress);
  ExecuteLocked(arena, run, &paths, &progress, out);
  progress.Phase = DreamPhase::Finished;
  progress.Outcome = out->Outcome;
  progress.Digests = out->Digests;
  progress.PagesChanged = out->PagesChanged;
  progress.Message = out->Message;
  DreamProgress_Save(arena, paths.Progress, &progress);
  ProcessFileLock_Release(&lock, paths.Lock);
}

DreamRestoreResult Dream_RestoreLatestSnapshot(Arena* arena,
                                               const WikiLocation* location,
                                               u32* restored, StrView* snapshot) {
  *restored = 0;
  *snapshot = {};
  DreamPaths paths = PathsFor(arena, location);
  Array<StrView> snapshots = SortedNames(arena, paths.Snapshots, true);
  if (!snapshots.Count) return DreamRestoreResult::NoSnapshot;
  if (!Fs_MkdirP(arena, location->DreamDir)) return DreamRestoreResult::Failed;
  ProcessFileLock lock = {};
  ProcessFileLockStatus locked = ProcessFileLock_TryAcquire(arena, paths.Lock, &lock);
  if (locked == ProcessFileLockStatus::Busy) return DreamRestoreResult::Busy;
  if (locked != ProcessFileLockStatus::Acquired) return DreamRestoreResult::Failed;
  *snapshot = snapshots.Data[snapshots.Count - 1];
  // Staged beside the pages so a failed copy leaves the live wiki untouched.
  StrView staging = Fs_Join(arena, location->DreamDir, SVL("restoring"));
  Fs_RemoveTree(arena, staging);
  u32 copied = 0;
  bool ok = CopyPages(arena, Fs_Join(arena, paths.Snapshots, *snapshot), staging, &copied) &&
            Fs_MkdirP(arena, location->Pages);
  Array<StrView> staged = ok ? SortedNames(arena, staging, false) : Array<StrView>{};
  for (u32 i = 0; ok && i < staged.Count; i++)
    ok = File_Replace(arena, Fs_Join(arena, staging, staged.Data[i]),
                      Fs_Join(arena, location->Pages, staged.Data[i]));
  Array<StrView> current = ok ? SortedNames(arena, location->Pages, false) : Array<StrView>{};
  for (u32 i = 0; ok && i < current.Count; i++) {
    bool kept = false;
    for (u32 k = 0; !kept && k < staged.Count; k++)
      kept = StrView_Eq(current.Data[i], staged.Data[k]);
    if (!kept) ok = Fs_Remove(arena, Fs_Join(arena, location->Pages, current.Data[i]));
  }
  Fs_RemoveTree(arena, staging);
  if (ok) *restored = copied;
  ProcessFileLock_Release(&lock, paths.Lock);
  return ok ? DreamRestoreResult::Restored : DreamRestoreResult::Failed;
}

void Dream_ReadStatus(Arena* arena, const WikiLocation* location, DreamStatus* out) {
  *out = {};
  DreamPaths paths = PathsFor(arena, location);
  out->Running = ProcessFileLock_IsHeld(arena, paths.Lock);
  out->Snapshots = SortedNames(arena, paths.Snapshots, true).Count;
  DreamState state = {};
  DreamState_Load(arena, paths.State, &state);
  Array<WikiJournalEntry> all = Array_Make<WikiJournalEntry>(arena, 64);
  u64 end = 0;
  WikiJournal_Read(arena, location->Journal, 0, &all, &end);
  for (u32 i = 0; i < all.Count; i++) {
    if (all.Data[i].Kind != WikiJournalKind::Dream) continue;
    out->HasLastDream = true;
    out->LastDream = all.Data[i];
  }
  Array<WikiJournalEntry> recent = Array_Make<WikiJournalEntry>(arena, 16);
  WikiJournal_Read(arena, location->Journal, state.JournalOffset, &recent, &end);
  for (u32 i = 0; i < recent.Count; i++) {
    if (recent.Data[i].Kind == WikiJournalKind::Note) out->PendingNotes++;
    if (recent.Data[i].Kind != WikiJournalKind::Session) continue;
    bool repeat = false;
    for (u32 j = 0; j < i && !repeat; j++)
      repeat = recent.Data[j].Kind == WikiJournalKind::Session &&
               StrView_Eq(recent.Data[j].SessionId, recent.Data[i].SessionId);
    if (!repeat) out->PendingSessions++;
  }
}

DreamWatchState Dream_WatchState(bool running, const DreamProgress* progress,
                                 u64 sinceMs, u64 nowMs) {
  bool current = progress && progress->StartedMs >= sinceMs;
  if (running) {
    // Lock held with finished progress: the new pass has not written its own
    // yet.
    if (!current || progress->Phase == DreamPhase::Finished)
      return DreamWatchState::Starting;
    return progress->Phase == DreamPhase::Preparing ? DreamWatchState::Preparing
                                                    : DreamWatchState::Dreaming;
  }
  if (current)
    return progress->Phase == DreamPhase::Finished ? DreamWatchState::Finished
                                                   : DreamWatchState::Stopped;
  return nowMs >= sinceMs + DREAM_WATCH_START_TIMEOUT_MS ? DreamWatchState::NotStarted
                                                         : DreamWatchState::Starting;
}

bool Dream_ReadProgress(Arena* arena, const WikiLocation* location, bool* running,
                        DreamProgress* out) {
  DreamPaths paths = PathsFor(arena, location);
  *running = ProcessFileLock_IsHeld(arena, paths.Lock);
  return DreamProgress_Load(arena, paths.Progress, out);
}

SessionResult Dream_ReadMessages(Arena* arena, SessionRepository* sessions,
                                 const DreamProgress* progress, Array<Message>* out) {
  *out = Array_Make<Message>(arena, 16);
  SessionOpenOptions open = {};
  if (!sessions || !progress || !progress->ProjectKey.Length ||
      !DurableId_Parse(progress->SessionId, &open.SessionId))
    return SessionResult_Invalid("the dream has no session yet");
  open.ProjectKey = progress->ProjectKey;
  SessionLog log = {};
  SessionInfo info = {};
  Array<SessionRecord> records = {};
  SessionResult result = sessions->Open(sessions->Ctx, arena, &open, &log, &info);
  if (SessionResult_IsOk(result)) result = SessionLog_ReadAll(&log, arena, &records);
  SessionLog_Close(&log);
  if (!SessionResult_IsOk(result)) return result;
  for (u32 i = 0; i < records.Count; i++)
    if (records.Data[i].Kind == SessionRecordKind::Message)
      Array_Push(out, records.Data[i].MessageEntry.Value);
  return SessionResult_Ok(out->Count);
}

bool Dream_SpawnBackground(Arena* arena, StrView workingDirectory) {
  StrView executable = Sys_ExecutablePath(arena);
  StrView args[] = {SVL("--dream")};
  return executable.Length &&
         Exec_SpawnDetached(arena, executable, args, 1, workingDirectory);
}

namespace {

// Dreaming runs unattended, so an undecided repository counts as untrusted.
StrView DreamPromptTemplate(Arena* arena, const DreamCommandLine* options,
                            const WikiLocation* location,
                            DefaultProjectTrust defaultTrust, StrView* source) {
  StrView project = Fs_Join(arena, Fs_Join(arena, location->Repository, SVL(".clippi")),
                            SVL("DREAM.md"));
  StrView content = {};
  if (Fs_Exists(project)) {
    ProjectTrustStore store = {};
    bool loaded = ProjectTrustStore_Load(arena, options->TrustPath, &store) !=
                  ProjectTrustStoreLoadResult::Malformed;
    ProjectTrustResolution trust = ProjectTrust_Resolve(
        true, ProjectTrustOverride::Unspecified, &store,
        ProjectTrust_CanonicalPath(arena, location->Repository), defaultTrust, false);
    if (loaded && trust.Trusted &&
        ConfigFile_ReadAll(arena, project, &content) == ConfigFileReadResult::Success) {
      *source = project;
      return content;
    }
  }
  StrView global = Fs_Join(arena, options->AgentDirectory, SVL("DREAM.md"));
  if (ConfigFile_ReadAll(arena, global, &content) == ConfigFileReadResult::Success) {
    *source = global;
    return content;
  }
  *source = SVL("built-in");
  return PROMPT_DREAM;
}

}

int Dream_RunCommandLine(Arena* arena, const DreamCommandLine* options) {
  WikiLocation location = {};
  if (!Wiki_Locate(arena, options->Store, options->WorkingDirectory, &location)) {
    fprintf(stderr, "error: could not locate the memory wiki\n");
    return 1;
  }
  RuntimeSelection selected =
      LoadRuntimeSelection(arena, options->SettingsPath, options->ModelOverride);
  if (!selected.Settings.Memory) {
    fprintf(stderr, "error: memory is turned off in settings; run /memory on first\n");
    return 1;
  }
  if (!selected.Spec || !selected.Spec->CreateModel || !selected.Spec->CreateProvider) {
    fprintf(stderr, "error: no usable built-in provider is registered\n");
    return 1;
  }
  StrView modelId = options->ModelOverride        ? SV(options->ModelOverride)
                    : selected.Settings.DreamModel.Length ? selected.Settings.DreamModel
                                                  : selected.ModelId;
  StrView promptSource = {};
  StrView prompt = Dream_ExpandPrompt(
      arena, DreamPromptTemplate(arena, options, &location, selected.Settings.DefaultTrust, &promptSource),
      &location);

  AppAuth auth = {};
  auth.Mem = arena;
  auth.Path = options->AuthPath;
  JsonlRepository jsonl = {};
  JsonlRepository_Init(&jsonl, options->DataRoot);
  SessionRepository sessions = JsonlRepository_AsRepository(&jsonl);

  DreamRun run = {};
  run.Location = location;
  run.Sources = &sessions;
  run.Sessions = &sessions;
  run.ModelInfo = ModelCache_CreateModel(arena, options->AgentDirectory,
                                         selected.Spec, modelId);
  run.Profile = Model_ResolveRequestPreference(&run.ModelInfo, selected.Settings.Profile);
  run.ProviderImpl = selected.Spec->CreateProvider(arena, selected.Spec);
  run.AuthUser = &auth;
  run.ResolveAuth = AppResolveAuth;
  run.Reviewer = PermissionReviewer_ForProvider(
      arena, options->AgentDirectory, selected.Spec,
      ProviderModelChoice_Find(&selected.Settings.PermissionReviewerModels, selected.Spec->Id),
      run.ProviderImpl, run.AuthUser, run.ResolveAuth, &run.ReviewerModel);
  run.Retry = selected.Settings.Retry;
  run.TurnLimit = selected.Settings.TurnLimit;
  run.Compaction = selected.Settings.Compaction;
  run.SystemPrompt = prompt;
  run.Events = {nullptr, ReplSink};
  run.NowMs = NowMillis();

  printf("Dreaming over %.*s (prompt: %.*s, model: %.*s)\n",
         (int)location.Repository.Length, location.Repository.Data,
         (int)promptSource.Length, promptSource.Data, (int)modelId.Length, modelId.Data);
  Http_GlobalInit();
  FileLock_Init();
  DreamResult result = {};
  Dream_Execute(arena, &run, &result);
  Http_GlobalShutdown();
  sessions.Close(sessions.Ctx);

  if (result.Outcome == DreamOutcome::Failed) {
    fprintf(stderr, "\nerror: %.*s\n", (int)result.Message.Length, result.Message.Data);
    return 1;
  }
  if (result.Outcome == DreamOutcome::Dreamed)
    printf("\n%u session digest%s, %u page%s changed.\n", result.Digests,
           result.Digests == 1 ? "" : "s", result.PagesChanged,
           result.PagesChanged == 1 ? "" : "s");
  else
    printf("%.*s\n", (int)result.Message.Length, result.Message.Data);
  return 0;
}
