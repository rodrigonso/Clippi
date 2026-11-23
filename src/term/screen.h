#pragma once
#include "base/arena.h"
#include "base/str.h"
#include "term/cell.h"

// Owned by a single (render) thread.

typedef void (*ScreenWriter)(void* user, const char* data, u32 len);

struct Screen {
  Arena* Mem;
  u16 Cols, Rows;
  Cell* Front;
  Cell* Back;
  u32 Cap;
  ScreenWriter Write;
  void* WriteUser;
  bool ForceRepaint;
  bool CursorVisible;
  u16 CursorX, CursorY;
  StrBuf Out;
  bool OutInit;
};

void Screen_Init(Screen* s, Arena* mem, ScreenWriter w, void* user);
void Screen_Resize(Screen* s, u16 cols, u16 rows);
void Screen_BeginFrame(Screen* s);

u32 Screen_PutChar(Screen* s, u16 x, u16 y, u32 cp, Style st);

u16 Screen_PutText(Screen* s, u16 x, u16 y, StrView utf8, Style st, u16 maxXExclusive);

void Screen_Fill(Screen* s, u16 x0, u16 y0, u16 x1, u16 y1, Style st);

void Screen_SetCursor(Screen* s, bool visible, u16 x, u16 y);
void Screen_Flush(Screen* s);

Cell Screen_BackAt(const Screen* s, u16 x, u16 y);
Cell Screen_FrontAt(const Screen* s, u16 x, u16 y);
