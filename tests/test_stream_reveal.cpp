#include <string.h>

#include "base/arena.h"
#include "check.h"
#include "tui/stream_reveal.h"

namespace {

StrView AppendFrame(StreamReveal* reveal, StrBuf* out, u64 nowMs,
                    bool blocked = false) {
  StrView next = StreamReveal_NextFrame(reveal, nowMs, blocked);
  StrBuf_Append(out, next);
  return next;
}

u32 RunFrames(StreamReveal* reveal, StrBuf* out, u64 startMs, u64 stepMs,
              u32 maxFrames, bool blocked = false) {
  u32 frames = 0;
  u64 now = startMs;
  while (frames < maxFrames && StreamReveal_HasPending(reveal)) {
    AppendFrame(reveal, out, now, blocked);
    frames++;
    now += stepMs;
  }
  return frames;
}

StrView FillAscii(Arena* a, u32 length) {
  char* text = (char*)ArenaAlloc(a, length, 1);
  for (u32 i = 0; i < length; i++) text[i] = (char)('a' + (i % 26));
  return StrView{text, length};
}

}

void TestStreamReveal() {
  Arena a = ArenaCreate();

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StrView text = FillAscii(&a, 1024);
    StreamReveal_Push(&reveal, text);

    StrBuf out = StrBuf_Make(&a, text.Length + 1);
    StrView first = AppendFrame(&reveal, &out, 1000);
    CHECK_ALL(first.Length > 0, first.Length < text.Length,
              StreamReveal_HasPending(&reveal));

    u32 frames = RunFrames(&reveal, &out, 1016, 16, 4096);
    CHECK_ALL(!StreamReveal_HasPending(&reveal), frames > 1,
              out.Length == text.Length,
              memcmp(out.Data, text.Data, text.Length) == 0);
  }

  {
    StreamReveal shallow;
    StreamReveal deep;
    StreamReveal_Init(&shallow, &a);
    StreamReveal_Init(&deep, &a);
    StreamReveal_Push(&shallow, FillAscii(&a, 24));
    StreamReveal_Push(&deep, FillAscii(&a, 4096));

    StreamReveal_NextFrame(&shallow, 1000, false);
    StreamReveal_NextFrame(&deep, 1000, false);
    StrView shallowFrame = StreamReveal_NextFrame(&shallow, 1016, false);
    StrView deepFrame = StreamReveal_NextFrame(&deep, 1016, false);
    CHECK(deepFrame.Length > shallowFrame.Length);
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StreamReveal_Push(&reveal, FillAscii(&a, 100 * 1024));

    StrBuf out = StrBuf_Make(&a, 8192);
    for (u64 now = 1000; now <= 2000; now += 16) AppendFrame(&reveal, &out, now);
    CHECK_ALL(out.Length >= 800, out.Length <= 1024,
              StreamReveal_HasPending(&reveal));
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StreamReveal_Push(&reveal, FillAscii(&a, 8192));

    StreamReveal_NextFrame(&reveal, 1000, false);
    StrView wide = StreamReveal_NextFrame(&reveal, 1050, false);
    CHECK(wide.Length >= 40);
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    const char first[] = "A\xf0\x9f";
    const char second[] = "\x98\x80" "B";
    StreamReveal_Push(&reveal, StrView{first, (u32)sizeof(first) - 1});

    StrBuf out = StrBuf_Make(&a, 16);
    AppendFrame(&reveal, &out, 1000);
    CHECK_ALL(StrView_Eq(StrBuf_View(&out), SVL("A")),
              StreamReveal_NextFrame(&reveal, 1016, false).Length == 0);

    StreamReveal_Push(&reveal, StrView{second, (u32)sizeof(second) - 1});
    RunFrames(&reveal, &out, 1032, 16, 64);
    CHECK_ALL(!StreamReveal_HasPending(&reveal),
              StrView_Eq(StrBuf_View(&out), SVL("A\xf0\x9f\x98\x80" "B")));
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    const u32 kScalars = 512;
    char* text = (char*)ArenaAlloc(&a, kScalars * 3, 1);
    for (u32 i = 0; i < kScalars; i++) {
      text[i * 3 + 0] = '\xe2';
      text[i * 3 + 1] = '\x82';
      text[i * 3 + 2] = '\xac';
    }
    StrView euros{text, kScalars * 3};
    StreamReveal_Push(&reveal, euros);

    StrBuf out = StrBuf_Make(&a, euros.Length + 1);
    u64 now = 1000;
    bool aligned = true;
    while (StreamReveal_HasPending(&reveal)) {
      StrView frame = AppendFrame(&reveal, &out, now);
      if (frame.Length % 3 != 0) aligned = false;
      now += 16;
    }
    CHECK_ALL(aligned, out.Length == euros.Length,
              memcmp(out.Data, euros.Data, euros.Length) == 0);
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    const char invalid[] = "\xf0(\x8c(";
    StreamReveal_Push(&reveal, StrView{invalid, (u32)sizeof(invalid) - 1});
    StrBuf out = StrBuf_Make(&a, 16);
    RunFrames(&reveal, &out, 1000, 16, 64, true);
    CHECK_ALL(!StreamReveal_HasPending(&reveal),
              out.Length == sizeof(invalid) - 1,
              memcmp(out.Data, invalid, sizeof(invalid) - 1) == 0);

    const char incomplete[] = "\xe2\x82";
    StreamReveal_Push(&reveal, StrView{incomplete, (u32)sizeof(incomplete) - 1});
    StrBuf_Clear(&out);
    RunFrames(&reveal, &out, 1064, 16, 64, true);
    CHECK_ALL(!StreamReveal_HasPending(&reveal),
              out.Length == sizeof(incomplete) - 1,
              memcmp(out.Data, incomplete, sizeof(incomplete) - 1) == 0);
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StrView text = FillAscii(&a, 64 * 1024);
    StreamReveal_Push(&reveal, text);

    StrBuf out = StrBuf_Make(&a, text.Length + 1);
    for (u64 now = 1000; now <= 1000 + kRevealDeadlineMs; now += 16)
      AppendFrame(&reveal, &out, now, true);
    CHECK_ALL(!StreamReveal_HasPending(&reveal), out.Length == text.Length,
              memcmp(out.Data, text.Data, text.Length) == 0);
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StreamReveal_Push(&reveal, FillAscii(&a, 4096));

    StrBuf out = StrBuf_Make(&a, 8192);
    for (u64 now = 1000; now <= 1000 + kRevealDeadlineMs; now += 16) {
      StreamReveal_Push(&reveal, SVL("tail"));
      AppendFrame(&reveal, &out, now, true);
    }
    CHECK(!StreamReveal_HasPending(&reveal));
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StrView text = FillAscii(&a, 4096);
    StreamReveal_Push(&reveal, text);

    StrBuf out = StrBuf_Make(&a, text.Length + 1);
    AppendFrame(&reveal, &out, 1000, true);
    AppendFrame(&reveal, &out, 1000 + kRevealDeadlineMs + 1, true);
    CHECK_ALL(!StreamReveal_HasPending(&reveal), out.Length == text.Length);
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StrBuf first = StrBuf_Make(&a, 16);
    StrBuf_Append(&first, SVL("first"));
    StreamReveal_Adopt(&reveal, first);
    StrBuf out = StrBuf_Make(&a, 4096);
    RunFrames(&reveal, &out, 1000, 16, 64, true);
    CHECK(!StreamReveal_HasPending(&reveal));

    StrBuf second = StrBuf_Make(&a, 2048);
    StrBuf_Append(&second, FillAscii(&a, 1024));
    StreamReveal_Adopt(&reveal, second);
    StrView paced = AppendFrame(&reveal, &out, 2000);
    CHECK_ALL(paced.Length > 0, paced.Length < second.Length,
              StreamReveal_HasPending(&reveal));
    RunFrames(&reveal, &out, 2016, 16, 4096);
    CHECK_ALL(!StreamReveal_HasPending(&reveal),
              out.Length == 5 + second.Length);
  }

  {
    StreamReveal reveal;
    StreamReveal_Init(&reveal, &a);
    StrView text = FillAscii(&a, 4096);
    StreamReveal_Push(&reveal, text);

    StrBuf out = StrBuf_Make(&a, text.Length + 1);
    for (u32 i = 0; i < 64; i++) AppendFrame(&reveal, &out, 1000);
    u32 stalled = out.Length;
    CHECK_ALL(stalled > 0, stalled < text.Length,
              StreamReveal_HasPending(&reveal));

    for (u32 i = 0; i < 8; i++) AppendFrame(&reveal, &out, 900 - i);
    CHECK(out.Length == stalled);

    RunFrames(&reveal, &out, 2000, 16, 4096);
    CHECK_ALL(!StreamReveal_HasPending(&reveal), out.Length == text.Length,
              memcmp(out.Data, text.Data, text.Length) == 0);
  }

  ArenaRelease(&a);
}
