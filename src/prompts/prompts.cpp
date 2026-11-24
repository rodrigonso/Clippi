#include "prompts/prompts.h"

namespace {

bool MatchesAt(StrView text, u32 at, StrView key) {
  if (text.Length - at < key.Length + 4) return false;
  return text.Data[at] == '{' && text.Data[at + 1] == '{' &&
         memcmp(text.Data + at + 2, key.Data, key.Length) == 0 &&
         text.Data[at + 2 + key.Length] == '}' && text.Data[at + 3 + key.Length] == '}';
}

}

StrView Prompt_Expand(Arena* arena, StrView text, const PromptVar* vars, u32 count) {
  usize extra = 0;
  for (u32 i = 0; i < count; i++) extra += vars[i].Value.Length;
  StrBuf out = StrBuf_Make(arena, (u32)(text.Length + extra + 1));
  for (u32 i = 0; i < text.Length;) {
    const PromptVar* match = nullptr;
    if (text.Data[i] == '{')
      for (u32 v = 0; v < count && !match; v++)
        if (MatchesAt(text, i, vars[v].Key)) match = &vars[v];
    if (match) {
      StrBuf_Append(&out, match->Value);
      i += match->Key.Length + 4;
    } else {
      StrBuf_AppendChar(&out, text.Data[i++]);
    }
  }
  return StrBuf_View(&out);
}

bool Prompt_Split(StrView text, StrView key, StrView* before, StrView* after) {
  for (u32 i = 0; i < text.Length; i++) {
    if (!MatchesAt(text, i, key)) continue;
    *before = StrView{text.Data, i};
    u32 end = i + key.Length + 4;
    *after = StrView{text.Data + end, text.Length - end};
    return true;
  }
  return false;
}

u32 Prompt_Lines(StrView text, StrView* out, u32 capacity) {
  u32 count = 0;
  u32 start = 0;
  for (u32 i = 0; i <= text.Length && count < capacity; i++) {
    if (i < text.Length && text.Data[i] != '\n') continue;
    if (i > start) out[count++] = StrView{text.Data + start, i - start};
    start = i + 1;
  }
  return count;
}
