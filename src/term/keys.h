#pragma once
#include "base/basics.h"

enum class KeyKind : u8 {
  None,
  Char,
  Enter,
  Newline,
  Backspace,
  Delete,
  Tab,
  Left,
  Right,
  Up,
  Down,
  Home,
  End,
  ScrollUp,
  ScrollDown,
  MouseMove,
  MouseLeftDown,
  MouseLeftUp,
  WordLeft,
  WordRight,
  CtrlC,
  CtrlD,
  CtrlL,
  CtrlO,
  CtrlU,
  CtrlK,
  CtrlW,
  Escape,
  PasteStart,
  PasteEnd,
};

struct Key {
  KeyKind Kind;
  u32 Codepoint;
  bool Ctrl;
  bool Alt;
  bool Shift;
  u16 MouseX;
  u16 MouseY;
};

struct KeyDecoder {
  char Buf[4096];
  u32 Len;
  bool InPaste;
  bool PendingEscape;
};

void KeyDecoder_Init(KeyDecoder* d);
void KeyDecoder_Push(KeyDecoder* d, const char* data, u32 n);
bool KeyDecoder_Next(KeyDecoder* d, Key* out);
