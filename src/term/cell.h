#pragma once
#include "base/basics.h"
#include "base/str.h"

// All-zero is a blank cell with default colours, so buffers can be cleared with memset.

enum : u8 {
  ATTR_NONE = 0,
  ATTR_BOLD = 1 << 0,
  ATTR_DIM = 1 << 1,
  ATTR_ITALIC = 1 << 2,
  ATTR_UNDERLINE = 1 << 3,
  ATTR_REVERSE = 1 << 4,
  ATTR_STRIKE = 1 << 5,
};

// Index 0 is reserved as "default", so explicit palette colours use 1..255.
const u8 COL_DEFAULT = 0;

struct Style {
  u8 Fg;
  u8 Bg;
  u8 Attr;
};

inline Style StyleMake(u8 fg, u8 bg, u8 attr) { return Style{fg, bg, attr}; }
inline Style StyleDefault() { return Style{COL_DEFAULT, COL_DEFAULT, ATTR_NONE}; }
inline bool StyleEq(Style a, Style b) { return a.Fg == b.Fg && a.Bg == b.Bg && a.Attr == b.Attr; }

// Cube and grey ramp (16..255) only: terminals theme the first 16 entries.
u8 Color_NearestXterm256(u8 r, u8 g, u8 b);

const u32 CELL_CONT = 0xFFFFFFFFu;

struct Cell {
  u32 Ch;  // 0 = blank (rendered as space); CELL_CONT = wide-glyph tail
  Style St;
};

inline bool CellEq(Cell a, Cell b) { return a.Ch == b.Ch && StyleEq(a.St, b.St); }

u32 CharWidth(u32 cp);

// Never 0, so zero-width scalars still advance the cursor instead of stacking forever.
inline u32 CharCellWidth(u32 cp) {
  u32 width = CharWidth(cp);
  return width < 1 ? 1 : width;
}

inline bool Utf8IsContinuation(char c) { return ((u8)c & 0xC0) == 0x80; }

u32 Utf8Decode(StrView s, u32* i);

inline bool Utf8IsControl(u32 cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }

u32 TextWidth(StrView s);
