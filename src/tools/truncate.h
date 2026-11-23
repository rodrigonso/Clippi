#pragma once
#include "base/arena.h"
#include "base/str.h"

static const u32 TRUNC_MAX_LINES = 2000;
static const u32 TRUNC_MAX_BYTES = 50u * 1024u;

struct TruncResult {
  StrView Content;     // arena-owned if changed, else a slice of the input
  bool Truncated;
  bool ByBytes;
  u32 TotalLines;
  u32 OutputLines;
};

inline u32 Trunc_CountLines(StrView content) {
  if (content.Length == 0) return 0;
  u32 count = 0;
  for (u32 i = 0; i < content.Length; i++) {
    if (content.Data[i] == '\n') count++;
  }
  if (content.Data[content.Length - 1] != '\n') count++;
  return count;
}

inline void Trunc_AppendKb(StrBuf* b, u64 bytes) {
  u64 kbx10 = (bytes / 1024u) * 10u + ((bytes % 1024u) * 10u + 512u) / 1024u;
  StrBuf_AppendU64(b, kbx10 / 10u);
  StrBuf_AppendChar(b, '.');
  StrBuf_AppendU64(b, kbx10 % 10u);
  StrBuf_AppendCStr(b, "KB");
}

TruncResult Trunc_Head(Arena* arena, StrView content, u32 maxLines, u32 maxBytes);
TruncResult Trunc_Tail(Arena* arena, StrView content, u32 maxLines, u32 maxBytes);
