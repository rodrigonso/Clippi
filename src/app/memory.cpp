#include "app/memory.h"

#include "tools/fs.h"

namespace {

bool Active(const MemoryExtension* memory) {
  return memory->Enabled && memory->Pages.Length;
}

void ApplyWritableRoots(MemoryExtension* memory) {
  if (!memory->Harness) return;
  AgentHarness_SetWritableRoots(memory->Harness, memory->WritableRoots,
                                Active(memory) ? 1 : 0);
}

void AppendNote(Arena* arena, ToolOutcome* io, StrView note) {
  Array<Content> content = Array_Make<Content>(arena, io->Content.Count + 1);
  for (u32 i = 0; i < io->Content.Count; i++) Array_Push(&content, io->Content.Data[i]);
  Array_Push(&content, Content_Text(note));
  io->Content = content;
}

void SessionChanged(void* user, const AppSessionContext* session) {
  MemoryExtension* memory = (MemoryExtension*)user;
  ArenaReset(&memory->Mem);
  memory->Location = {};
  memory->Root = {};
  memory->Pages = {};
  memory->WritableRoots[0] = {};
  memory->Session = {};
  memory->JournaledMs = 0;
  memory->WorkingDirectory = StrView_Dup(&memory->Mem, session->WorkingDirectory);
  if (Wiki_Locate(&memory->Mem, memory->Store, memory->WorkingDirectory,
                  &memory->Location) &&
      Fs_MkdirP(&memory->Mem, memory->Location.Pages)) {
    memory->Root = Fs_PermissionPath(&memory->Mem, memory->Location.Root,
                                     memory->Location.Root);
    memory->Pages = Fs_PermissionPath(&memory->Mem, memory->Location.Root,
                                      memory->Location.Pages);
    memory->WritableRoots[0] = memory->Pages;
  }
  if (session->SessionId.Length) {
    memory->Session.Kind = WikiJournalKind::Session;
    memory->Session.SessionId = StrView_Dup(&memory->Mem, session->SessionId);
    memory->Session.ProjectKey = StrView_Dup(&memory->Mem, session->ProjectKey);
    memory->Session.WorkingDirectory = memory->WorkingDirectory;
    memory->Session.TranscriptPath =
        StrView_Dup(&memory->Mem, session->TranscriptPath);
  }
  ApplyWritableRoots(memory);
}

// Journaled at the first prompt: a session that never gets one is never
// written.
SessionResult BeforeRun(void* user, Arena* arena, Message*) {
  MemoryExtension* memory = (MemoryExtension*)user;
  u64 now = NowMillis();
  if (Active(memory) && memory->Session.SessionId.Length &&
      (!memory->JournaledMs || now - memory->JournaledMs >= MEMORY_REJOURNAL_MS)) {
    memory->Session.TimeMs = now;
    if (WikiJournal_Append(arena, memory->Location.Journal, &memory->Session))
      memory->JournaledMs = now;
  }
  return SessionResult_Ok();
}

void BeforeTool(void* user, Arena* arena, StrView toolName, StrView,
                const ToolArgs* args, AgentToolGateDecision* out) {
  MemoryExtension* memory = (MemoryExtension*)user;
  if (!Active(memory)) return;
  StrView target = Fs_MutationTarget(arena, toolName, args, memory->WorkingDirectory);
  if (!target.Length || !Fs_PathContained(memory->Root, target) ||
      Fs_PathContained(memory->Pages, target))
    return;
  StrBuf reason = StrBuf_Make(arena, memory->Pages.Length + 160);
  StrBuf_AppendCStr(&reason,
                    "The memory wiki's journal and dreaming state belong to "
                    "Clippi. Write wiki pages under ");
  StrBuf_Append(&reason, Fs_ToPosix(arena, memory->Pages));
  StrBuf_AppendCStr(&reason, " instead.");
  out->Block = true;
  out->Reason = StrBuf_View(&reason);
}

void AfterTool(void* user, Arena* arena, StrView toolName, StrView toolCallId,
               const ToolArgs* args, ToolOutcome* io) {
  MemoryExtension* memory = (MemoryExtension*)user;
  if (!Active(memory) || io->IsError) return;
  StrView target = Fs_MutationTarget(arena, toolName, args, memory->WorkingDirectory);
  if (!target.Length || Fs_PathEquals(target, memory->Pages) ||
      !Fs_PathContained(memory->Pages, target))
    return;

  StrView name = Fs_Base(target);
  if (!Fs_PathEquals(Fs_Parent(target), memory->Pages) || name.Length <= 3 ||
      !StrView_EndsWith(name, SVL(".md"))) {
    AppendNote(arena, io,
               SVL("Memory wiki: only <slug>.md files directly in the pages "
                   "folder are part of the wiki, so this file is ignored."));
    return;
  }

  StrView slug = {name.Data, name.Length - 3};
  Wiki wiki = {};
  Wiki_Scan(arena, memory->Pages, &wiki);
  const WikiPage* written = Wiki_Find(&wiki, slug);
  Array<WikiIssue> issues = Array_Make<WikiIssue>(arena, 8);
  if (written) Wiki_LintPage(arena, &wiki, written, &issues);
  if (issues.Count) {
    StrBuf note = StrBuf_Make(arena, 256);
    StrBuf_AppendCStr(&note, "Memory wiki: ");
    StrBuf_Append(&note, slug);
    StrBuf_AppendCStr(&note, ".md was saved but needs fixing:\n");
    Wiki_AppendIssues(&note, &issues);
    AppendNote(arena, io, StrBuf_View(&note));
  }

  WikiJournalEntry entry = {};
  entry.Kind = WikiJournalKind::Page;
  entry.TimeMs = NowMillis();
  entry.SessionId = memory->Session.SessionId;
  entry.Slug = slug;
  entry.ToolCallId = toolCallId;
  WikiJournal_Append(arena, memory->Location.Journal, &entry);
}

void Shutdown(void* user) {
  MemoryExtension* memory = (MemoryExtension*)user;
  if (memory->Harness) AgentHarness_SetWritableRoots(memory->Harness, nullptr, 0);
  ArenaRelease(&memory->Mem);
}

bool RememberExec(void* ctx, Arena* arena, StrView toolCallId, const ToolArgs* args,
                  volatile i32*, const ToolUpdateSink*, ToolResult* out, StrBuf* err) {
  MemoryExtension* memory = (MemoryExtension*)ctx;
  StrView note = StrView_Trim(ToolArgs_GetStr(args, "note"));
  if (!note.Length) {
    StrBuf_AppendCStr(err, "missing 'note' argument");
    return false;
  }
  if (note.Length > WIKI_NOTE_MAX_BYTES) {
    StrBuf_AppendCStr(err, "'note' is limited to ");
    StrBuf_AppendU64(err, WIKI_NOTE_MAX_BYTES);
    StrBuf_AppendCStr(err, " bytes; keep it to one fact");
    return false;
  }
  if (!memory || !Active(memory)) {
    StrBuf_AppendCStr(err, "the memory wiki is off, so nothing was saved");
    return false;
  }
  WikiJournalEntry entry = {};
  entry.Kind = WikiJournalKind::Note;
  entry.TimeMs = NowMillis();
  entry.SessionId = memory->Session.SessionId;
  entry.ToolCallId = toolCallId;
  entry.Text = note;
  if (!WikiJournal_Append(arena, memory->Location.Journal, &entry)) {
    StrBuf_AppendCStr(err, "the note could not be saved to the memory journal");
    return false;
  }
  out->Content = Array_Make<Content>(arena, 1);
  Array_Push(&out->Content,
             Content_Text(SVL("Noted. The next dream will file it into the memory wiki.")));
  return true;
}

bool RememberDescribePermission(void*, Arena* arena, const ToolArgs* args,
                                const ToolPermissionScope*, ToolPermissionInfo* out,
                                StrBuf* err) {
  StrView note = StrView_Trim(ToolArgs_GetStr(args, "note"));
  if (!note.Length) {
    StrBuf_AppendCStr(err, "missing 'note' argument");
    return false;
  }
  StrBuf action = StrBuf_Make(arena, note.Length + 32);
  StrBuf_AppendCStr(&action, "Remember for the memory wiki: ");
  StrBuf_Append(&action, note);
  out->Action = StrBuf_View(&action);
  out->GrantKey = SVL("remember");
  // Same footing as a page write, which a note becomes once a dream files it.
  out->AutoAllow = true;
  return true;
}

constexpr StrView kRememberPromptGuidelines[] = {
    SVL("Use remember to note a fact a future session would need -- a user "
        "preference or correction, a decision, a non-obvious project fact -- "
        "when writing a wiki page now would interrupt the task. Skip what the "
        "code or docs already say.")};

}

Tool RememberTool(MemoryExtension* memory) {
  Tool t = {};
  t.Name = SVL("remember");
  t.Label = SVL("Remember");
  t.Description = SVL(
      "Save one durable fact for the memory wiki; the next dream files it into "
      "the right page.");
  t.PromptSnippet = SVL("Note a durable fact for the memory wiki");
  Tool_SetPromptGuidelines(&t, kRememberPromptGuidelines);
  t.ParamsJsonSchema = SVL(
      "{\"type\":\"object\",\"properties\":{\"note\":{\"type\":\"string\","
      "\"description\":\"One self-contained fact in a sentence or two\"}},"
      "\"required\":[\"note\"],\"additionalProperties\":false}");
  t.PermissionEffect = ToolPermissionEffect::Sensitive;
  t.DescribePermission = RememberDescribePermission;
  t.Ctx = memory;
  t.Execute = RememberExec;
  return t;
}

void MemoryExtension_Init(MemoryExtension* memory, AgentHarness* harness,
                          StrView store, bool enabled) {
  *memory = MemoryExtension{};
  memory->Harness = harness;
  memory->Store = store;
  memory->Enabled = enabled;
  memory->Mem = ArenaCreate((usize)16 << 20);
}

AppExtension MemoryExtension_Make(MemoryExtension* memory) {
  AppExtension extension = {};
  extension.User = memory;
  extension.Hooks.BeforeRun = BeforeRun;
  extension.Hooks.BeforeTool = BeforeTool;
  extension.Hooks.AfterTool = AfterTool;
  extension.SessionChanged = SessionChanged;
  extension.Shutdown = Shutdown;
  return extension;
}

void MemoryExtension_SetEnabled(MemoryExtension* memory, bool enabled) {
  if (!memory) return;
  memory->Enabled = enabled;
  ApplyWritableRoots(memory);
}
