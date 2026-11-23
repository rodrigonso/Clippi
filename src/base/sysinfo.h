#pragma once
#include <time.h>

#include "base/arena.h"
#include "base/basics.h"
#include "base/str.h"

u64 PeakRssBytes();

u64 CurrentRssBytes();

StrView Sys_CurrentDir(Arena* a);

StrView Sys_ExecutablePath(Arena* a);

bool Sys_SetCurrentDir(StrView path);

StrView Sys_HomeDir(Arena* a);

StrView Sys_AbbreviatePath(Arena* a, StrView path, StrView home);

StrView Sys_GitBranch(Arena* a, StrView dir);

// May contain `..` segments; canonicalize before comparing.
StrView Sys_GitCommonDir(Arena* a, StrView dir, StrView* workTree = nullptr);

bool Sys_LocalTime(u64 timestampMs, struct tm* out);

StrView Git_GitdirFromFile(StrView contents);
StrView Git_BranchFromHead(StrView contents);
StrView Git_CommonDirFromFile(StrView contents);
