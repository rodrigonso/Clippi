#include "term/sanitize.h"

#include "term/cell.h"

namespace {

const u32 kTabStop = 8;

bool InRange(unsigned char c, unsigned char lo, unsigned char hi) { return c >= lo && c <= hi; }

// Only CSI and OSC are full sequences; any other ESC-led byte drops just the ESC, matching pi's stripAnsi.
u32 ConsumeEscape(StrView in, u32 i) {
  u32 n = in.Length;
  u32 j = i + 1;
  if (j >= n) return j;
  unsigned char c = (unsigned char)in.Data[j];

  if (c == '[') {
    j++;
    while (j < n && InRange((unsigned char)in.Data[j], 0x20, 0x3F)) j++;
    if (j < n && InRange((unsigned char)in.Data[j], 0x40, 0x7E)) j++;
    return j;
  }
  if (c == ']') {
    j++;
    while (j < n) {
      unsigned char d = (unsigned char)in.Data[j];
      if (d == 0x07 || d == 0x9C) {
        j++;
        break;
      }
      if (d == 0x1B && j + 1 < n && in.Data[j + 1] == '\\') {
        j += 2;
        break;
      }
      j++;
    }
    return j;
  }
  return j;
}

}

void Sanitize_Display(StrBuf* out, StrView in) {
  u32 col = 0;
  u32 i = 0;
  while (i < in.Length) {
    if ((unsigned char)in.Data[i] == 0x1B) {
      i = ConsumeEscape(in, i);
      continue;
    }

    u32 j = i;
    u32 cp = Utf8Decode(in, &j);

    if (cp == 0x0A) {
      StrBuf_AppendChar(out, '\n');
      col = 0;
      i = j;
      continue;
    }
    if (cp == 0x09) {
      u32 pad = kTabStop - (col % kTabStop);
      for (u32 k = 0; k < pad; k++) StrBuf_AppendChar(out, ' ');
      col += pad;
      i = j;
      continue;
    }
    // Carriage return, other C0, DEL, and C1 (U+009B is a one-character CSI): drop.
    if (Utf8IsControl(cp)) {
      i = j;
      continue;
    }

    if (cp == 0xFFFD)
      StrBuf_AppendUtf8(out, cp);  // malformed bytes never reach the terminal raw
    else
      StrBuf_Append(out, StrView{in.Data + i, j - i});
    col += CharWidth(cp);
    i = j;
  }
}
