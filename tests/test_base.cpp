#include <string.h>

#include "base/arena.h"
#include "base/array.h"
#include "base/str.h"
#include "base/sysinfo.h"
#include "base/thread.h"
#include "check.h"

static bool IsSingleLine(StrView text) {
  for (u32 i = 0; i < text.Length; i++) {
    if (text.Data[i] == '\n' || text.Data[i] == '\r') return false;
  }
  return true;
}

void TestArena() {
  Arena a = ArenaCreate();
  void* p1 = ArenaAlloc(&a, 100, 16);
  CHECK(((usize)p1 % 16) == 0);
  usize mark = a.Used;
  void* p2 = ArenaAlloc(&a, 64, 16);
  CHECK((byte*)p2 >= (byte*)p1 + 100);
  ArenaRestore(&a, mark);
  CHECK(a.Used == mark);
  ArenaReset(&a);
  CHECK(a.Used == 0);
  byte* big = (byte*)ArenaAlloc(&a, 5 * 1024 * 1024, 1);
  big[0] = 1;
  big[5 * 1024 * 1024 - 1] = 2;
  CHECK(big[0] == 1 && big[5 * 1024 * 1024 - 1] == 2);

  usize peak = a.Committed;
  CHECK(peak >= 5 * 1024 * 1024);
  ArenaReset(&a);
  CHECK_ALL(a.Used == 0, a.Committed == peak);

  ArenaTrim(&a, 64 * 1024);
  CHECK_ALL(a.Committed < peak, a.Committed <= 64 * 1024, a.Used == 0);

  byte* again = (byte*)ArenaAlloc(&a, 2 * 1024 * 1024, 1);
  again[0] = 3;
  again[2 * 1024 * 1024 - 1] = 4;
  CHECK_ALL(again[0] == 3, again[2 * 1024 * 1024 - 1] == 4,
            a.Committed >= 2 * 1024 * 1024);

  usize before = a.Committed;
  ArenaReset(&a);
  ArenaTrim(&a, before * 2);
  CHECK(a.Committed == before);

  ArenaTrim(&a, (usize)0 - 1);
  CHECK(a.Committed == before);

  ArenaRelease(&a);

  Arena secret = ArenaCreate();
  byte* bytes = (byte*)ArenaAlloc(&secret, 513, 1);
  memset(bytes, 0x5a, 513);
  usize committed = secret.Committed;
  ArenaResetSecure(&secret);
  bool cleared = true;
  for (u32 i = 0; i < 513; i++) cleared = cleared && bytes[i] == 0;
  CHECK_ALL(cleared, secret.Used == 0, secret.Committed == committed);
  bytes = (byte*)ArenaAlloc(&secret, 513, 1);
  CHECK(bytes[512] == 0);
  ArenaReleaseSecure(&secret);
  CHECK_ALL(secret.Base == nullptr, secret.Used == 0, secret.Committed == 0);
  ArenaReleaseSecure(&secret);
}

void TestArray() {
  Arena a = ArenaCreate();
  Array<int> arr = Array_Make<int>(&a, 2);
  for (int i = 0; i < 100; i++) Array_Push(&arr, i);
  CHECK_ALL(arr.Count == 100, *Array_Get(&arr, 50) == 50,
            *Array_Last(&arr) == 99);
  CHECK(Array_Pop(&arr) == 99);
  CHECK_ALL(arr.Count == 99, Array_Get(&arr, 999) == nullptr);
  ArenaRelease(&a);
}

void TestStr() {
  Arena a = ArenaCreate();
  StrBuf b = StrBuf_Make(&a, 4);
  StrBuf_AppendCStr(&b, "hello");
  StrBuf_AppendChar(&b, ' ');
  StrBuf_AppendCStr(&b, "world");
  StrBuf_AppendU64(&b, 123);
  CHECK_ALL(strcmp(StrBuf_CStr(&b), "hello world123") == 0, b.Length == 14);
  StrView v = StrBuf_View(&b);
  CHECK_ALL(StrView_EqCStr(v, "hello world123"),
            StrView_Eq(SVL("abc"), SV("abc")),
            !StrView_Eq(SVL("abc"), SV("abd")));
  CHECK_ALL(
      StrView_ContainsInsensitiveAscii(SVL("Alpha Beta"), SVL("PHa")),
      StrView_ContainsInsensitiveAscii(SVL("Alpha Beta"), SVL("BETA")),
      StrView_ContainsInsensitiveAscii(SVL("Alpha Beta"), StrView{}),
      !StrView_ContainsInsensitiveAscii(SVL("Alpha Beta"), SVL("gamma")),
      !StrView_ContainsInsensitiveAscii(SVL("short"), SVL("longer")),
      StrView_ContainsInsensitiveAscii(SVL("caf\xc3\xa9"), SVL("\xc3\xa9")),
      !StrView_ContainsInsensitiveAscii(SVL("caf\xc3\xa9"), SVL("\xc3\x89")));
  CHECK_ALL(
      StrView_EqInsensitiveAscii(SVL("Retry-After"), SVL("retry-after")),
      StrView_EqInsensitiveAscii(StrView{}, StrView{}),
      !StrView_EqInsensitiveAscii(SVL("true"), SVL("true ")),
      !StrView_EqInsensitiveAscii(SVL("false"), SVL("true")),
      StrView_EqInsensitiveAscii(SVL("A\0B"), SVL("a\0b")),
      !StrView_EqInsensitiveAscii(SVL("\xc3\xa9"), SVL("\xc3\x89")),
      StrView_Eq(StrView_Trim(SVL(" \t\r\n\f\vtext \t\r\n\f\v")), SVL("text")),
      StrView_Trim(StrView{}).Data == nullptr);
  CHECK(StrView_EqCStr(StrView_Trim(SVL("  hello \t\r\n")), "hello"));
  CHECK(StrView_EqCStr(StrView_Trim(SVL("hello")), "hello"));
  CHECK(StrView_Trim(SVL("   ")).Length == 0);
  CHECK(StrView_Trim(StrView{}).Length == 0);
  CHECK(StrView_EqCStr(StrView_Trim(SVL("  a b  ")), "a b"));

  usize mark = a.Used;
  StrView lf = SVL("already\nnormalized\n");
  CHECK_ALL(StrView_NormalizeLf(&a, lf).Data == lf.Data,
            StrView_NormalizeLf(&a, StrView{}).Data == nullptr);
  CHECK(a.Used == mark);
  StrView normalized = StrView_NormalizeLf(&a, SVL("a\r\nb\rc\n\r\r\n"));
  CHECK_ALL(StrView_Eq(normalized, SVL("a\nb\nc\n\n\n")),
            normalized.Data[normalized.Length] == '\0',
            StrView_Eq(StrView_NormalizeLf(&a, SVL("a\0\r\nb")), SVL("a\0\nb")));
  const char bounded[] = {'x', '\r', 'y'};
  CHECK(StrView_NormalizeLf(&a, StrView{bounded, 1}).Data == bounded);

  StrBuf neg = StrBuf_Make(&a, 4);
  StrBuf_AppendI64(&neg, -42);
  CHECK(strcmp(StrBuf_CStr(&neg), "-42") == 0);

  StrView dup = StrView_Dup(&a, SV("copy me"));
  CHECK(StrView_EqCStr(dup, "copy me"));
  ArenaRelease(&a);
}

void TestHostInfo() {
  Arena a = ArenaCreate();

  u64 currentRss = CurrentRssBytes();
  u64 peakRss = PeakRssBytes();
  CHECK_ALL(currentRss > 0, peakRss >= currentRss);

  {
    u64 now = NowMillis();
    CHECK_ALL(now > 1577836800000ull, now < 4102444800000ull);

    bool sawSubSecond = (now % 1000ull) != 0;
    for (int i = 0; i < 40 && !sawSubSecond; i++) {
      Sleep_Ms(3);
      if (NowMillis() % 1000ull != 0) sawSubSecond = true;
    }
    CHECK(sawSubSecond);

    u64 before = NowMillis();
    Sleep_Ms(30);
    u64 after = NowMillis();
    CHECK_ALL(after >= before, after - before < 5000ull);
  }

  CHECK_ALL(StrView_EqCStr(Git_BranchFromHead(SVL("ref: refs/heads/main\n")), "main"),
            StrView_EqCStr(
                Git_BranchFromHead(SVL("ref: refs/heads/worktree/swift-mortar-e0e4\n")),
                "worktree/swift-mortar-e0e4"),
            StrView_EqCStr(Git_BranchFromHead(SVL("ref: refs/heads/feature/x")),
                           "feature/x"));
  CHECK(Git_BranchFromHead(SVL("0123456789abcdef0123456789abcdef01234567\n")).Length == 0);

  CHECK_ALL(StrView_EqCStr(
                Git_GitdirFromFile(SVL("gitdir: C:/repo/.git/worktrees/wt/\n")),
                "C:/repo/.git/worktrees/wt"),
            StrView_EqCStr(Git_GitdirFromFile(SVL("gitdir: ../.git/modules/sub")),
                           "../.git/modules/sub"),
            Git_GitdirFromFile(SVL("not a gitdir file")).Length == 0);

  StrView home = SVL("C:\\Users\\rodri");
  CHECK_ALL(
      StrView_EqCStr(Sys_AbbreviatePath(&a, SVL("C:\\Users\\rodri\\.worktrees\\wt"), home),
                     "~\\.worktrees\\wt"),
      StrView_EqCStr(Sys_AbbreviatePath(&a, home, home), "~"));
  CHECK(StrView_EqCStr(Sys_AbbreviatePath(&a, SVL("C:\\Users\\rodrigo\\x"), home), "C:\\Users\\rodrigo\\x"));
  CHECK(StrView_EqCStr(Sys_AbbreviatePath(&a, SVL("D:\\proj"), StrView{nullptr, 0}), "D:\\proj"));

  StrView cwd = Sys_CurrentDir(&a);
  CHECK(cwd.Length > 0);
  StrView executable = Sys_ExecutablePath(&a);
  CHECK(executable.Length > 0);
  if (executable.Length) {
    CHECK(executable.Data[executable.Length] == 0);
#if defined(_WIN32)
    CHECK((executable.Length >= 3 && executable.Data[1] == ':') ||
          executable.Data[0] == '\\');
#else
    CHECK(executable.Data[0] == '/');
#endif
    CHECK(Sys_SetCurrentDir(SVL("..")));
    CHECK(StrView_Eq(Sys_ExecutablePath(&a), executable));
    CHECK(Sys_SetCurrentDir(cwd));
  }
  CHECK_ALL(Sys_SetCurrentDir(cwd),
            !Sys_SetCurrentDir(SVL("clippi-directory-that-does-not-exist")));
  bool hasSep = false;
  for (u32 i = 0; i < cwd.Length; i++)
    if (cwd.Data[i] == '/' || cwd.Data[i] == '\\') hasSep = true;
  CHECK(hasSep);
  StrView branch = Sys_GitBranch(&a, cwd);
  CHECK(IsSingleLine(branch));

  ArenaRelease(&a);
}
