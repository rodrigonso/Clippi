#pragma once
#include "base/arena.h"
#include "base/array.h"
#include "base/str.h"

struct FsStat {
  bool Exists;
  bool IsDir;
  u64 Size;
  bool IsRegular;
  u64 ModifiedMs;
};

FsStat Fs_Stat(StrView path);
bool Fs_Exists(StrView path);
bool Fs_IsDir(StrView path);
bool Fs_MkdirP(Arena* arena, StrView path);
StrView Fs_RealPath(Arena* arena, StrView path);
bool Fs_Remove(Arena* arena, StrView path);
// Links and junctions inside are removed, never followed.
bool Fs_RemoveTree(Arena* arena, StrView path);

constexpr u64 FS_TOOL_MAX_FILE_BYTES = (u64)64 << 20;

struct FsPermissionTarget {
  StrView Path;
  bool Contained;
  bool Protected;
};

struct ToolArgs;
struct ToolPermissionInfo;
struct ToolPermissionScope;

// Security: ambiguous paths and missing roots are never contained. `Protected`
// (.git/.clippi/.agents below every containing root) is never auto-allowed.
FsPermissionTarget Fs_ResolvePermissionTarget(Arena* arena,
                                              const ToolPermissionScope* scope,
                                              StrView target);
bool Fs_DescribeMutationPermission(Arena* arena, const ToolArgs* args,
                                   const ToolPermissionScope* scope,
                                   StrView actionPrefix,
                                   StrView grantPrefix,
                                   ToolPermissionInfo* out, StrBuf* err);

StrView Fs_PermissionPath(Arena* arena, StrView workingDirectory, StrView path);

StrView Fs_MutationTarget(Arena* arena, StrView toolName, const ToolArgs* args,
                          StrView workingDirectory);

// Inputs must already be canonical.
bool Fs_PathContained(StrView root, StrView path);

bool Fs_IsSep(char c);
char Fs_PathFold(char c);
bool Fs_PathEquals(StrView a, StrView b);
StrView Fs_Parent(StrView path);
StrView Fs_Base(StrView path);
StrView Fs_Ext(StrView path);
StrView Fs_Join(Arena* arena, StrView a, StrView b);
StrView Fs_ToPosix(Arena* arena, StrView path);
StrView Fs_Relativize(Arena* arena, StrView root, StrView path);

struct FsEntry {
  StrView Name;
  bool IsDir;
  u64 Size;
  bool IsSymlink;
  bool IsRegular;
};

Array<FsEntry> Fs_ListDir(Arena* arena, StrView dir, bool* ok);

// Names borrowed until `visit` returns; buffers taken from `scratch` stay there.
typedef bool (*FsEntryVisitor)(void* user, const FsEntry& entry);
bool Fs_ForEachEntry(Arena* scratch, StrView dir, volatile i32* abort, StrBuf* err,
                     FsEntryVisitor visit, void* user);

struct FsIgnorePattern {
  StrView BaseRel;
  StrView Pattern;
  bool DirOnly;
  bool Anchored;
  bool Negated;
};

void Fs_LoadIgnoreFile(Arena* arena, Array<FsIgnorePattern>* patterns,
                       StrView dirAbs, StrView dirRel, StrView fileName);
bool Fs_IsIgnored(const Array<FsIgnorePattern>* patterns, StrView relPath, bool isDir);

typedef bool (*FsWalkFilter)(void* user, StrView relPath, const FsEntry& entry,
                             bool* include, StrBuf* err);

struct FsWalkOptions {
  bool RespectGitignore;
  bool IncludeDirs;
  bool SkipLinks;
  bool RespectIgnoreFiles;
  bool LoadParentIgnores;
  volatile i32* Abort;
  FsWalkFilter Filter;
  void* FilterUser;
};

// Paths and names are borrowed until the callback returns. Directory links are
// never descended into, even when SkipLinks is false.
typedef bool (*FsWalkVisitor)(void* user, StrView absPath, StrView relPath, const FsEntry& e);
bool Fs_WalkChecked(Arena* arena, StrView root, const FsWalkOptions& opts,
                    FsWalkVisitor visit, void* user, StrBuf* err);
