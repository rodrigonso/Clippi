#include <stdlib.h>

#include "base/thread.h"
#include "json/json.h"
#include "tools/fs.h"
#include "tools/tools.h"
#include "tools/truncate.h"

static int Ls_CompareEntries(const void* a, const void* b) {
  const FsEntry* ea = (const FsEntry*)a;
  const FsEntry* eb = (const FsEntry*)b;
  u32 n = ClippiMin(ea->Name.Length, eb->Name.Length);
  for (u32 i = 0; i < n; i++) {
    char ca = Ascii_ToLower(ea->Name.Data[i]);
    char cb = Ascii_ToLower(eb->Name.Data[i]);
    if (ca < cb) return -1;
    if (ca > cb) return 1;
  }
  if (ea->Name.Length < eb->Name.Length) return -1;
  if (ea->Name.Length > eb->Name.Length) return 1;
  // Names equal under folding ("A" and "a") still get one fixed order, so the
  // cut at `limit` does not depend on the order the directory streams them.
  return StrView_Compare(ea->Name, eb->Name);
}

// The `Limit` first entries in sort order, found without holding the whole
// directory: a max-heap whose top is the entry a smaller newcomer evicts.
struct LsSelection {
  Array<FsEntry> Heap;
  Arena Names;  // heap names, plus evicted ones until the next compaction
  usize LiveBytes;
  usize DeadBytes;
  u64 Total;
  u32 Limit;
};

static bool Ls_Above(const FsEntry& a, const FsEntry& b) { return Ls_CompareEntries(&a, &b) > 0; }

static void Ls_SiftDown(Array<FsEntry>* heap, u32 i) {
  for (;;) {
    u32 largest = i;
    u32 left = 2 * i + 1;
    u32 right = left + 1;
    if (left < heap->Count && Ls_Above(heap->Data[left], heap->Data[largest])) largest = left;
    if (right < heap->Count && Ls_Above(heap->Data[right], heap->Data[largest])) largest = right;
    if (largest == i) return;
    FsEntry swap = heap->Data[i];
    heap->Data[i] = heap->Data[largest];
    heap->Data[largest] = swap;
    i = largest;
  }
}

static void Ls_Compact(LsSelection* s) {
  Arena fresh = ArenaCreate();
  for (u32 i = 0; i < s->Heap.Count; i++)
    s->Heap.Data[i].Name = StrView_Dup(&fresh, s->Heap.Data[i].Name);
  ArenaRelease(&s->Names);
  s->Names = fresh;
  s->DeadBytes = 0;
}

static bool Ls_Visit(void* user, const FsEntry& entry) {
  LsSelection* s = (LsSelection*)user;
  s->Total++;
  if (s->Heap.Count == s->Limit) {
    if (!Ls_Above(s->Heap.Data[0], entry)) return true;
    s->LiveBytes -= s->Heap.Data[0].Name.Length;
    s->DeadBytes += s->Heap.Data[0].Name.Length;
    s->Heap.Data[0] = entry;
    s->Heap.Data[0].Name = StrView_Dup(&s->Names, entry.Name);
    s->LiveBytes += entry.Name.Length;
    Ls_SiftDown(&s->Heap, 0);
    if (s->DeadBytes > s->LiveBytes + 64 * 1024) Ls_Compact(s);
    return true;
  }
  FsEntry kept = entry;
  kept.Name = StrView_Dup(&s->Names, entry.Name);
  s->LiveBytes += entry.Name.Length;
  Array_Push(&s->Heap, kept);
  for (u32 i = s->Heap.Count - 1; i > 0;) {
    u32 parent = (i - 1) / 2;
    if (!Ls_Above(s->Heap.Data[i], s->Heap.Data[parent])) break;
    FsEntry swap = s->Heap.Data[i];
    s->Heap.Data[i] = s->Heap.Data[parent];
    s->Heap.Data[parent] = swap;
    i = parent;
  }
  return true;
}

bool LsExec(void* ctx, Arena* arena, StrView id, const ToolArgs* args,
            volatile i32* abort, const ToolUpdateSink* updates,
            ToolResult* out, StrBuf* err) {
  (void)ctx;
  (void)id;
  (void)updates;
  StrView path = ToolArgs_GetStr(args, "path");
  if (path.Length == 0) path = SVL(".");
  i64 limit = ToolArgs_GetInt(args, "limit", 500);
  if (limit <= 0) limit = 500;
  if (limit > (i64)0xFFFFFFFFu) limit = (i64)0xFFFFFFFFu;

  FsStat stat = Fs_Stat(path);
  if (!stat.Exists) {
    StrBuf_AppendCStr(err, "Path not found: ");
    StrBuf_Append(err, path);
    return false;
  }
  if (!stat.IsDir) {
    StrBuf_AppendCStr(err, "Not a directory: ");
    StrBuf_Append(err, path);
    return false;
  }

  LsSelection selection = {};
  selection.Limit = (u32)limit;
  selection.Heap = Array_Make<FsEntry>(arena, selection.Limit < 64u ? selection.Limit : 64u);
  selection.Names = ArenaCreate();
  if (!Fs_ForEachEntry(arena, path, abort, nullptr, Ls_Visit, &selection)) {
    ArenaRelease(&selection.Names);
    if (abort && Atomic_LoadI32(abort)) {
      StrBuf_AppendCStr(err, "aborted");
    } else {
      StrBuf_AppendCStr(err, "Cannot list directory: ");
      StrBuf_Append(err, path);
    }
    return false;
  }

  Array<FsEntry>* entries = &selection.Heap;
  qsort(entries->Data, entries->Count, sizeof(FsEntry), Ls_CompareEntries);

  StrBuf raw = StrBuf_Make(arena, 4096);
  for (u32 i = 0; i < entries->Count; i++) {
    StrBuf_Append(&raw, entries->Data[i].Name);
    if (entries->Data[i].IsDir) StrBuf_AppendChar(&raw, '/');
    StrBuf_AppendChar(&raw, '\n');
  }
  ArenaRelease(&selection.Names);
  if (selection.Total == 0) StrBuf_AppendCStr(&raw, "(empty directory)");

  TruncResult tr = Trunc_Head(arena, StrBuf_View(&raw), 0xFFFFFFFFu, TRUNC_MAX_BYTES);
  StrBuf result = StrBuf_Make(arena, tr.Content.Length + 128);
  StrBuf_Append(&result, tr.Content);

  bool limitReached = selection.Total > (u64)limit;
  bool byteLimitReached = tr.Truncated && tr.ByBytes;
  if (limitReached || byteLimitReached) {
    if (result.Length > 0 && result.Data[result.Length - 1] != '\n') StrBuf_AppendChar(&result, '\n');
    StrBuf_AppendChar(&result, '[');
    if (limitReached) {
      StrBuf_AppendI64(&result, limit);
      StrBuf_AppendCStr(&result, " entries limit reached. Use limit=");
      StrBuf_AppendU64(&result, (u64)limit * 2u);
      StrBuf_AppendCStr(&result, " for more.");
    }
    if (byteLimitReached) {
      if (limitReached) StrBuf_AppendChar(&result, ' ');
      Trunc_AppendKb(&result, TRUNC_MAX_BYTES);
      StrBuf_AppendCStr(&result, " limit reached");
    }
    StrBuf_AppendChar(&result, ']');
  }

  out->Content = Array_Make<Content>(arena, 1);
  Array_Push(&out->Content, Content_Text(StrBuf_View(&result)));
  return true;
}

Tool LsTool() {
  Tool t = {};
  t.Name = SVL("ls");
  t.Label = SVL("List");
  t.Description = SVL("List files and directories in a directory.");
  t.PromptSnippet = SVL("List directory contents");
  t.ParamsJsonSchema = SVL(
      "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Directory "
      "to list (default: current directory)\"},\"limit\":{\"type\":\"number\",\"description\":"
      "\"Maximum number of entries to return (default: 500)\"}}}");
  t.Execute = LsExec;
  t.PermissionEffect = ToolPermissionEffect::Inspection;
  return t;
}
