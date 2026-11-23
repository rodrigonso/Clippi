#pragma once
#include "tui/linemodel.h"

// Deliberate divergences from pi (docs/UI.md): no OSC 8 links (Screen holds one scalar
// per cell), no fence highlighting or LaTeX, no GFM alignment, wide list markers nest.

// Nearest xterm-256 entries to pi's dark theme; Style has no 24-bit colour.
struct MarkdownPalette {
  u8 Heading;
  u8 Code;
  u8 Link;
  u8 LinkUrl;
  u8 CodeBlock;
  u8 CodeBlockBorder;
  u8 Quote;
  u8 QuoteBorder;
  u8 Hr;
  u8 ListBullet;
  u8 TableBorder;
};

const MarkdownPalette& Markdown_DefaultPalette();

struct MarkdownOpts {
  // Passed per render, never cached: LineModel_FitsAtWidth re-renders at a trial width.
  u16 Width;
  bool Color;
  u8 Accent;
};

inline MarkdownOpts MarkdownOpts_Make(u16 width, bool color, u8 accent = 0) {
  MarkdownOpts opts;
  opts.Width = width ? width : 80;
  opts.Color = color;
  opts.Accent = accent;
  return opts;
}

// Rows carry their wrap prefix in SpanLine::Continuation; callers adding an outer
// prefix must concatenate onto it rather than overwrite it.
void Markdown_Render(Arena* a, StrView md, Array<SpanLine>* out, MarkdownOpts opts);

// Spans borrow `line` except the " (url)" link suffix, which is allocated in `a`.
void Markdown_Inline(Arena* a, StrView line, Style base, Array<Span>* out,
                     bool color = true);
