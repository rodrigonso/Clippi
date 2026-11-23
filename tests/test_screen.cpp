#include "base/arena.h"
#include "base/str.h"
#include "check.h"
#include "term/screen.h"
#include "term/term.h"

namespace {

struct Cap {
  StrBuf B;
};

void CapWrite(void* u, const char* d, u32 n) { StrBuf_Append(&((Cap*)u)->B, StrView{d, n}); }

}

void TestScreen() {
  Arena a = ArenaCreate();

  StrBuf osc52 = StrBuf_Make(&a, 8);
  Term_AppendOSC52(&osc52, StrView{nullptr, 0});
  CHECK(StrView_Eq(StrBuf_View(&osc52), SVL("\x1b]52;c;\x07")));

  const char binary[] = {'\0', (char)(unsigned char)0xff, '\x10', 'f'};
  StrBuf_Clear(&osc52);
  StrBuf_AppendCStr(&osc52, "before:");
  Term_AppendOSC52(&osc52, StrView{binary, sizeof(binary)});
  CHECK(StrView_Eq(StrBuf_View(&osc52), SVL("before:\x1b]52;c;AP8QZg==\x07")));

  CHECK_ALL(Color_NearestXterm256(0x00, 0x78, 0xd4) == 32,
            Color_NearestXterm256(0x10, 0x89, 0x3e) == 29,
            Color_NearestXterm256(0xff, 0xaf, 0x00) == 214,
            Color_NearestXterm256(0x98, 0x98, 0x9d) == 247,
            Color_NearestXterm256(0x00, 0x00, 0x00) == 16,
            Color_NearestXterm256(0xff, 0xff, 0xff) == 231);

  Cap cap;
  cap.B = StrBuf_Make(&a, 256);

  Screen s;
  Screen_Init(&s, &a, CapWrite, &cap);
  Screen_Resize(&s, 10, 3);
  Screen_SetCursor(&s, false, 0, 0);

  Screen_BeginFrame(&s);
  Screen_PutText(&s, 0, 0, SVL("hi"), StyleDefault(), 10);
  CHECK_ALL(Screen_BackAt(&s, 0, 0).Ch == 'h',
            Screen_BackAt(&s, 1, 0).Ch == 'i');
  StrBuf_Clear(&cap.B);
  Screen_Flush(&s);
  CHECK_ALL(Screen_FrontAt(&s, 0, 0).Ch == 'h',
            Screen_FrontAt(&s, 1, 0).Ch == 'i',
            StrView_Contains(StrBuf_View(&cap.B), "hi"));

  Screen_BeginFrame(&s);
  Screen_PutText(&s, 0, 0, SVL("ho"), StyleDefault(), 10);
  StrBuf_Clear(&cap.B);
  Screen_Flush(&s);
  StrView out2 = StrBuf_View(&cap.B);
  CHECK_ALL(StrView_Contains(out2, "o"),
            !StrView_Contains(out2, "h"),
            Screen_FrontAt(&s, 1, 0).Ch == 'o');

  Screen_BeginFrame(&s);
  Screen_PutText(&s, 0, 0, SVL("ho"), StyleDefault(), 10);
  StrBuf_Clear(&cap.B);
  Screen_Flush(&s);
  StrView out3 = StrBuf_View(&cap.B);
  CHECK(!StrView_Contains(out3, "o"));

  Screen_BeginFrame(&s);
  Screen_PutText(&s, 0, 2, SVL("R"), StyleMake(196, COL_DEFAULT, ATTR_BOLD), 10);
  StrBuf_Clear(&cap.B);
  Screen_Flush(&s);
  StrView out4 = StrBuf_View(&cap.B);
  CHECK_ALL(StrView_Contains(out4, "38;5;196"), StrView_Contains(out4, "R"));

  Screen_BeginFrame(&s);
  Screen_PutText(&s, 0, 1, SVL("\xe4\xbd\xa0X"), StyleDefault(), 10);
  CHECK_ALL(Screen_BackAt(&s, 0, 1).Ch == 0x4F60,
            Screen_BackAt(&s, 1, 1).Ch == CELL_CONT,
            Screen_BackAt(&s, 2, 1).Ch == 'X');

  s.ForceRepaint = false;
  Screen_Resize(&s, 10, 3);
  CHECK_ALL(!s.ForceRepaint,
            Screen_BackAt(&s, 0, 1).Ch == 0x4F60);

  Screen_BeginFrame(&s);
  Screen_PutText(&s, 0, 0, SVL("\x1b" "a\xc2\x9b" "b\xc2\x1b"), StyleDefault(), 10);
  CHECK_ALL(Screen_BackAt(&s, 0, 0).Ch == 0xFFFD, Screen_BackAt(&s, 1, 0).Ch == 'a',
            Screen_BackAt(&s, 2, 0).Ch == 0xFFFD, Screen_BackAt(&s, 3, 0).Ch == 'b',
            Screen_BackAt(&s, 4, 0).Ch == 0xFFFD, Screen_BackAt(&s, 5, 0).Ch == 0xFFFD);
  StrBuf_Clear(&cap.B);
  Screen_Flush(&s);
  StrView controlled = StrBuf_View(&cap.B);
  CHECK_ALL(!StrView_Contains(controlled, "\x1b" "a"), !StrView_Contains(controlled, "\xc2\x9b"));

  Screen_Resize(&s, 6, 2);
  CHECK(s.ForceRepaint);

  ArenaRelease(&a);
}
