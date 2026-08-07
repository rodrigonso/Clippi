#pragma once

#include "base/str.h"

// Every per-user config and data file lives under one directory. Building that
// prefix in each module drifted: three call sites fell back to "." when the
// home directory was unknown and one did not, which produced an absolute
// "/.clippi" instead of a relative path.

// <home>/.clippi, or ./.clippi when the home directory is unknown.
StrView ClippiDataRoot(Arena* arena);

// <home>/.clippi/<name>, with the same fallback.
StrView ClippiDataPath(Arena* arena, const char* name);
