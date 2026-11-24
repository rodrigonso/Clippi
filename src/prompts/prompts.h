#pragma once

#include "base/arena.h"
#include "base/str.h"
#include "prompts/prompt_text.h"

struct PromptVar {
  StrView Key;  // without braces: "tools" matches {{tools}}
  StrView Value;
};

// Replaces each {{key}} with its value; unknown placeholders stay as written.
StrView Prompt_Expand(Arena* arena, StrView text, const PromptVar* vars, u32 count);

// Splits `text` around the single {{key}}; false when it is absent.
bool Prompt_Split(StrView text, StrView key, StrView* before, StrView* after);

// One entry per line, for prompt files that hold a list.
u32 Prompt_Lines(StrView text, StrView* out, u32 capacity);
