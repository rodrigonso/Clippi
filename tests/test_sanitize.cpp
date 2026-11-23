#include "base/arena.h"
#include "check.h"
#include "term/sanitize.h"

namespace {

StrView San(Arena* a, StrView in) {
  StrBuf b = StrBuf_Make(a, 64);
  Sanitize_Display(&b, in);
  return StrBuf_View(&b);
}
StrView SanC(Arena* a, const char* s) { return San(a, SV(s)); }

bool AllSpaces(StrView s, u32 from, u32 to) {
  for (u32 i = from; i < to; i++)
    if (s.Data[i] != ' ') return false;
  return true;
}

}

void TestSanitize() {
  Arena a = ArenaCreate();

  CHECK(San(&a, StrView{nullptr, 0}).Length == 0);

  struct SanitizeCase {
    const char* Input;
    const char* Expected;
  };
  SanitizeCase cases[] = {
      {"hello world", "hello world"},
      {"a\r\nb\r\n", "a\nb\n"},
      {"x\ry", "xy"},
      {"line\r", "line"},
      {"a\nb", "a\nb"},
      {" Volume Serial Number is 6C52-1E84\r\n Directory of C:\\x\r\n",
       " Volume Serial Number is 6C52-1E84\n Directory of C:\\x\n"},
      {"\033[31mred\033[0m", "red"},
      {"\033[2J\033[Hhi", "hi"},
      {"\033[38;5;244mX\033[0m", "X"},
      {"\033]0;window title\007Z", "Z"},
      {"\033]8;;http://x\033\\link\033]8;;\033\\", "link"},
      {"ab\033", "ab"},
      {"a\033cb", "acb"},
      {"a\001\002b", "ab"},
      {"a\177b", "ab"},
      {"h\303\251llo", "h\303\251llo"},
      {"ok\xC2\033[31mred", "ok\xEF\xBF\xBDred"},
      {"\xC2\033]0;title\xC2\033\\Z", "\xEF\xBF\xBDZ"},
      {"a\xE4\xB8\033[2Jb", "a\xEF\xBF\xBD\xEF\xBF\xBD" "b"},
      {"\xC2\x9B" "31mX", "31mX"},
      {"\x9B" "31mX", "\xEF\xBF\xBD" "31mX"},
      {"\xEF\xBF\xBD", "\xEF\xBF\xBD"},
  };
  bool sanitized = true;
  for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    bool matches = StrView_EqCStr(SanC(&a, cases[i].Input), cases[i].Expected);
    sanitized = matches && sanitized;
  }
  CHECK(sanitized);

  {
    StrView s = SanC(&a, "\tx");
    CHECK_ALL(s.Length == 9, AllSpaces(s, 0, 8) && s.Data[8] == 'x');
  }
  {
    StrView s = SanC(&a, "a\tb");
    CHECK_ALL(s.Length == 9,
              s.Data[0] == 'a' && AllSpaces(s, 1, 8) && s.Data[8] == 'b');
  }
  {
    StrView s = SanC(&a, "ab\n\tc");
    CHECK_ALL(s.Length == 12,
              s.Data[2] == '\n' && AllSpaces(s, 3, 11) && s.Data[11] == 'c');
  }

  {
    const char raw[] = {'a', '\0', 'b'};
    CHECK(StrView_EqCStr(San(&a, StrView{raw, 3}), "ab"));
  }

  {
    StrView s = SanC(&a, "\344\270\255\tx");
    CHECK_ALL(s.Length == 10,
              (unsigned char)s.Data[0] == 0xE4 &&
                  (unsigned char)s.Data[1] == 0xB8 &&
                  (unsigned char)s.Data[2] == 0xAD,
              AllSpaces(s, 3, 9) && s.Data[9] == 'x');
  }

  ArenaRelease(&a);
}
