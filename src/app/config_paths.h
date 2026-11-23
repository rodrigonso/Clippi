#pragma once

#include "base/str.h"

StrView ClippiDataRoot(Arena* arena);

StrView ClippiDataPath(Arena* arena, const char* name);
