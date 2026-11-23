#include "term/cell.h"

namespace {

const u8 CUBE_LEVELS[6] = {0, 95, 135, 175, 215, 255};

u32 AbsDiff(u32 a, u32 b) { return a > b ? a - b : b - a; }

u32 NearestCubeLevel(u8 v) {
  u32 best = 0;
  for (u32 i = 1; i < 6; i++)
    if (AbsDiff(v, CUBE_LEVELS[i]) < AbsDiff(v, CUBE_LEVELS[best])) best = i;
  return best;
}

u32 Distance2(u8 r, u8 g, u8 b, u32 cr, u32 cg, u32 cb) {
  i32 dr = (i32)r - (i32)cr;
  i32 dg = (i32)g - (i32)cg;
  i32 db = (i32)b - (i32)cb;
  return (u32)(dr * dr + dg * dg + db * db);
}

}  // namespace

u8 Color_NearestXterm256(u8 r, u8 g, u8 b) {
  u32 ri = NearestCubeLevel(r);
  u32 gi = NearestCubeLevel(g);
  u32 bi = NearestCubeLevel(b);
  u32 cubeDistance =
      Distance2(r, g, b, CUBE_LEVELS[ri], CUBE_LEVELS[gi], CUBE_LEVELS[bi]);

  // Grey ramp 232..255 runs from 8 to 238 in steps of 10.
  u32 average = ((u32)r + g + b) / 3;
  u32 step = average < 8 ? 0 : (average - 8 + 5) / 10;
  if (step > 23) step = 23;
  u32 grey = 8 + 10 * step;
  if (Distance2(r, g, b, grey, grey, grey) < cubeDistance) return (u8)(232 + step);
  return (u8)(16 + 36 * ri + 6 * gi + bi);
}

u32 CharWidth(u32 cp) {
  if (cp == 0) return 1;  // blank cell renders as a space

  // Zero-width: combining marks, joiners, variation selectors, BOM.
  if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
      (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x200B && cp <= 0x200F) ||
      (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF)
    return 0;

  // Wide: CJK + fullwidth + emoji.
  if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
      (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
      (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
      (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
      (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
      (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) ||
      (cp >= 0x20000 && cp <= 0x3FFFD))
    return 2;

  return 1;
}

u32 Utf8Decode(StrView s, u32* i) {
  if (*i >= s.Length) return 0;
  const u8* p = (const u8*)s.Data + *i;
  u32 length = StrView_Utf8SequenceLength(s, *i);
  // Only the offending byte is consumed: whatever follows a bad lead byte is
  // decoded on its own, so an ESC can never hide inside a "continuation".
  if (!length) {
    *i += 1;
    return 0xFFFD;
  }
  *i += length;
  switch (length) {
    case 1: return p[0];
    case 2: return ((u32)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    case 3: return ((u32)(p[0] & 0x0F) << 12) | ((u32)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    default:
      return ((u32)(p[0] & 0x07) << 18) | ((u32)(p[1] & 0x3F) << 12) |
             ((u32)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
  }
}

u32 TextWidth(StrView s) {
  u32 i = 0, w = 0;
  while (i < s.Length) {
    u32 cp = Utf8Decode(s, &i);
    w += CharWidth(cp);
  }
  return w;
}
