#include "base/arena.h"
#include "check.h"
#include "tui/markdown.h"

namespace {

const MarkdownPalette& Pal() { return Markdown_DefaultPalette(); }

bool SpanHas(const Array<Span>* a, const char* needle, u8 attrMask, u8 fg) {
  for (u32 i = 0; i < a->Count; i++) {
    Span sp = a->Data[i];
    if (!StrView_EqCStr(sp.Text, needle)) continue;
    if (attrMask && (sp.St.Attr & attrMask) != attrMask) continue;
    if (fg != 0 && sp.St.Fg != fg) continue;
    return true;
  }
  return false;
}

StrView RowText(Arena* a, const SpanLine* line) {
  StrBuf out = StrBuf_Make(a, 64);
  for (u32 i = 0; i < line->Spans.Count; i++) StrBuf_Append(&out, line->Spans.Data[i].Text);
  return StrBuf_View(&out);
}

bool RowIs(Arena* a, const Array<SpanLine>* rows, u32 index, const char* expected) {
  if (index >= rows->Count) return false;
  return StrView_EqCStr(RowText(a, &rows->Data[index]), expected);
}

bool AnyRowIs(Arena* a, const Array<SpanLine>* rows, const char* expected) {
  for (u32 i = 0; i < rows->Count; i++)
    if (RowIs(a, rows, i, expected)) return true;
  return false;
}

Style StyleOf(const Array<SpanLine>* rows, const char* needle, bool* found) {
  for (u32 r = 0; r < rows->Count; r++) {
    const Array<Span>* spans = &rows->Data[r].Spans;
    for (u32 i = 0; i < spans->Count; i++) {
      if (StrView_EqCStr(spans->Data[i].Text, needle)) {
        *found = true;
        return spans->Data[i].St;
      }
    }
  }
  *found = false;
  return StyleDefault();
}

Array<SpanLine> Render(Arena* a, const char* md, u16 width = 80) {
  Array<SpanLine> out = Array_Make<SpanLine>(a, 8);
  Markdown_Render(a, SV(md), &out, MarkdownOpts_Make(width, true));
  return out;
}

void TestInline(Arena* a) {
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("a **b** c"), StyleDefault(), &out);
    CHECK(SpanHas(&out, "b", ATTR_BOLD, 0));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("x `code` y"), StyleDefault(), &out);
    CHECK(SpanHas(&out, "code", 0, Pal().Code));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("_em_"), StyleDefault(), &out);
    CHECK(SpanHas(&out, "em", ATTR_ITALIC, 0));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("***x***"), StyleDefault(), &out);
    CHECK_ALL(SpanHas(&out, "x", (u8)(ATTR_BOLD | ATTR_ITALIC), 0),
              !SpanHas(&out, "*x", 0, 0));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("snake_case_name"), StyleDefault(), &out);
    for (u32 i = 0; i < out.Count; i++) CHECK((out.Data[i].St.Attr & ATTR_ITALIC) == 0);
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("~~gone~~"), StyleDefault(), &out);
    CHECK(SpanHas(&out, "gone", ATTR_STRIKE, 0));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("~~ spaced ~~"), StyleDefault(), &out);
    for (u32 i = 0; i < out.Count; i++) CHECK((out.Data[i].St.Attr & ATTR_STRIKE) == 0);
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("\\*not bold\\*"), StyleDefault(), &out);
    StrBuf flat = StrBuf_Make(a, 32);
    for (u32 i = 0; i < out.Count; i++) {
      StrBuf_Append(&flat, out.Data[i].Text);
      CHECK((out.Data[i].St.Attr & ATTR_BOLD) == 0);
    }
    CHECK(StrView_EqCStr(StrBuf_View(&flat), "*not bold*"));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("see [t](http://u) end"), StyleDefault(), &out);
    CHECK_ALL(SpanHas(&out, "t", ATTR_UNDERLINE, Pal().Link),
              SpanHas(&out, " (http://u)", 0, Pal().LinkUrl));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("go https://x.dev/a now"), StyleDefault(), &out);
    CHECK_ALL(SpanHas(&out, "https://x.dev/a", ATTR_UNDERLINE, Pal().Link),
              !SpanHas(&out, " (https://x.dev/a)", 0, 0));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("see https://x.dev."), StyleDefault(), &out);
    CHECK(SpanHas(&out, "https://x.dev", ATTR_UNDERLINE, Pal().Link));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("<https://x.dev>"), StyleDefault(), &out);
    CHECK(SpanHas(&out, "https://x.dev", ATTR_UNDERLINE, Pal().Link));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("![alt](u.png)"), StyleDefault(), &out);
    StrBuf flat = StrBuf_Make(a, 32);
    for (u32 i = 0; i < out.Count; i++) StrBuf_Append(&flat, out.Data[i].Text);
    CHECK(StrView_EqCStr(StrBuf_View(&flat), "alt"));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("`a *b* c`"), StyleDefault(), &out);
    CHECK(SpanHas(&out, "a *b* c", 0, Pal().Code));
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("`plain`"), StyleDefault(), &out, false);
    CHECK_ALL(out.Count == 1, out.Data[0].St.Fg == COL_DEFAULT);
  }
  {
    Array<Span> out = Array_Make<Span>(a, 4);
    Markdown_Inline(a, SVL("2 * 3 = 6"), StyleDefault(), &out);
    StrBuf flat = StrBuf_Make(a, 32);
    for (u32 i = 0; i < out.Count; i++) StrBuf_Append(&flat, out.Data[i].Text);
    CHECK(StrView_EqCStr(StrBuf_View(&flat), "2 * 3 = 6"));
  }
}

void TestHeadings(Arena* a) {
  {
    Array<SpanLine> out = Render(a, "# Title");
    CHECK_ALL(out.Count == 1, RowIs(a, &out, 0, "Title"),
              SpanHas(&out.Data[0].Spans, "Title", (u8)(ATTR_BOLD | ATTR_UNDERLINE),
                      Pal().Heading));
  }
  {
    Array<SpanLine> out = Render(a, "## Two");
    bool found = false;
    Style st = StyleOf(&out, "Two", &found);
    CHECK_ALL(found, (st.Attr & ATTR_BOLD) != 0, (st.Attr & ATTR_UNDERLINE) == 0);
  }
  {
    Array<SpanLine> out = Render(a, "### Three");
    CHECK(RowIs(a, &out, 0, "### Three"));
  }
  {
    Array<SpanLine> out = Render(a, "## Two ##");
    CHECK(RowIs(a, &out, 0, "Two"));
  }
  {
    Array<SpanLine> out = Render(a, "Title\n=====");
    bool found = false;
    Style st = StyleOf(&out, "Title", &found);
    CHECK_ALL(out.Count == 1, found, (st.Attr & ATTR_UNDERLINE) != 0);
  }
  {
    Array<SpanLine> out = Render(a, "Sub\n---");
    bool found = false;
    Style st = StyleOf(&out, "Sub", &found);
    CHECK_ALL(out.Count == 1, found, (st.Attr & ATTR_BOLD) != 0,
              (st.Attr & ATTR_UNDERLINE) == 0);
  }
  {
    Array<SpanLine> out = Render(a, "# H\n\nbody");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 0, "H"), RowIs(a, &out, 1, ""),
              RowIs(a, &out, 2, "body"));
  }
  {
    Array<SpanLine> out = Render(a, "# A `b` C");
    CHECK(SpanHas(&out.Data[0].Spans, "b", 0, Pal().Code));
  }
}

void TestCodeBlocks(Arena* a) {
  {
    Array<SpanLine> out = Render(a, "```\ncode\n```");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 0, "```"), RowIs(a, &out, 1, "  code"),
              RowIs(a, &out, 2, "```"));
    CHECK(SpanHas(&out.Data[1].Spans, "code", 0, Pal().CodeBlock));
  }
  {
    Array<SpanLine> out = Render(a, "```rust\nfn main() {}\n```");
    CHECK_ALL(RowIs(a, &out, 0, "```rust"), RowIs(a, &out, 1, "  fn main() {}"));
  }
  {
    Array<SpanLine> out = Render(a, "~~~\n```\n~~~");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 1, "  ```"));
  }
  {
    Array<SpanLine> out = Render(a, "```\n# not a heading\n```");
    CHECK(RowIs(a, &out, 1, "  # not a heading"));
  }
  {
    Array<SpanLine> out = Render(a, "    indented");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 0, "```"), RowIs(a, &out, 1, "  indented"));
  }
  {
    Array<SpanLine> out = Render(a, "```\nx\n```");
    CHECK_ALL(out.Data[1].HasContinuation,
              StrView_EqCStr(out.Data[1].Continuation.Text, "  "));
  }
}

void TestLists(Arena* a) {
  {
    Array<SpanLine> out = Render(a, "* item");
    CHECK_ALL(out.Count == 1, RowIs(a, &out, 0, "- item"));
    CHECK(SpanHas(&out.Data[0].Spans, "- ", 0, Pal().ListBullet));
  }
  {
    Array<SpanLine> out = Render(a, "3. a\n4. b");
    CHECK_ALL(out.Count == 2, RowIs(a, &out, 0, "3. a"), RowIs(a, &out, 1, "4. b"));
  }
  {
    Array<SpanLine> out = Render(a, "- top\n    - nested");
    CHECK_ALL(out.Count == 2, RowIs(a, &out, 0, "- top"),
              RowIs(a, &out, 1, "    - nested"));
  }
  {
    Array<SpanLine> out = Render(a, "- a\n    - b\n        - c");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 1, "    - b"),
              RowIs(a, &out, 2, "        - c"));
  }
  {
    Array<SpanLine> out = Render(a, "1. top\n   - nested");
    CHECK(RowIs(a, &out, 1, "    - nested"));
  }
  {
    Array<SpanLine> out = Render(a, "- [x] done\n- [ ] todo");
    CHECK_ALL(RowIs(a, &out, 0, "- [x] done"), RowIs(a, &out, 1, "- [ ] todo"));
  }
  {
    Array<SpanLine> out = Render(a, "- item");
    CHECK_ALL(out.Data[0].HasContinuation,
              StrView_EqCStr(out.Data[0].Continuation.Text, "  "));
  }
  {
    Array<SpanLine> out = Render(a, "10. item");
    CHECK(StrView_EqCStr(out.Data[0].Continuation.Text, "    "));
  }
  {
    Array<SpanLine> out = Render(a, "- [x] done");
    CHECK(StrView_EqCStr(out.Data[0].Continuation.Text, "      "));
  }
  {
    Array<SpanLine> out = Render(a, "- a\n\n- b");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 0, "- a"), RowIs(a, &out, 1, ""),
              RowIs(a, &out, 2, "- b"));
  }
  {
    Array<SpanLine> out = Render(a, "- a\n- b");
    CHECK(out.Count == 2);
  }
  {
    Array<SpanLine> out = Render(a, "- **bold** item");
    CHECK(SpanHas(&out.Data[0].Spans, "bold", ATTR_BOLD, 0));
  }
  {
    Array<SpanLine> out = Render(a, "- text\n\n      code");
    CHECK(AnyRowIs(a, &out, "  ```"));
  }
  {
    Array<SpanLine> out = Render(a, "- one\n  two");
    CHECK_ALL(out.Count == 2, RowIs(a, &out, 0, "- one"), RowIs(a, &out, 1, "  two"));
  }
}

void TestQuotes(Arena* a) {
  {
    Array<SpanLine> out = Render(a, "> quoted");
    CHECK_ALL(out.Count == 1, RowIs(a, &out, 0, "\xe2\x94\x82 quoted"));
    bool found = false;
    Style st = StyleOf(&out, "quoted", &found);
    CHECK_ALL(found, (st.Attr & ATTR_ITALIC) != 0, st.Fg == Pal().Quote);
    CHECK(SpanHas(&out.Data[0].Spans, "\xe2\x94\x82 ", 0, Pal().QuoteBorder));
  }
  {
    Array<SpanLine> out = Array_Make<SpanLine>(a, 4);
    Markdown_Render(a, SVL("> > deep"), &out, MarkdownOpts_Make(80, true, 33));
    bool found = false;
    Style st = StyleOf(&out, "deep", &found);
    CHECK_ALL(out.Count == 1, found, st.Fg == Pal().Quote,
              out.Data[0].Spans.Count >= 2,
              out.Data[0].Spans.Data[0].St.Fg == 33,
              out.Data[0].Spans.Data[1].St.Fg == 33);
  }
  {
    Array<SpanLine> out = Render(a, "> quoted");
    CHECK_ALL(out.Data[0].HasContinuation,
              StrView_EqCStr(out.Data[0].Continuation.Text, "\xe2\x94\x82 "));
  }
  {
    Array<SpanLine> out = Render(a, "> > deep");
    CHECK(RowIs(a, &out, 0, "\xe2\x94\x82 \xe2\x94\x82 deep"));
  }
  {
    Array<SpanLine> out = Render(a, "> one\ntwo");
    CHECK_ALL(out.Count == 2, RowIs(a, &out, 0, "\xe2\x94\x82 one"),
              RowIs(a, &out, 1, "\xe2\x94\x82 two"));
  }
  {
    Array<SpanLine> out = Render(a, "> - item");
    CHECK(RowIs(a, &out, 0, "\xe2\x94\x82 - item"));
  }
  {
    Array<SpanLine> out = Render(a, "> a `b` c");
    CHECK(SpanHas(&out.Data[0].Spans, "b", ATTR_ITALIC, Pal().Code));
  }
}

void TestRules(Arena* a) {
  {
    Array<SpanLine> out = Render(a, "---", 10);
    CHECK_ALL(out.Count == 1, TextWidth(RowText(a, &out.Data[0])) == 10,
              out.Data[0].Spans.Data[0].St.Fg == Pal().Hr);
  }
  {
    Array<SpanLine> out = Render(a, "***", 200);
    CHECK(TextWidth(RowText(a, &out.Data[0])) == 80);
  }
  {
    Array<SpanLine> out = Render(a, "___", 8);
    CHECK(TextWidth(RowText(a, &out.Data[0])) == 8);
  }
  {
    Array<SpanLine> out = Render(a, "a\n\n---\n\nb");
    CHECK_ALL(out.Count == 5, RowIs(a, &out, 0, "a"), RowIs(a, &out, 4, "b"));
  }
}

void TestTables(Arena* a) {
  const char* md = "| a | b |\n|---|---|\n| 1 | 2 |";
  {
    Array<SpanLine> out = Render(a, md);
    CHECK(out.Count == 5);
    CHECK_ALL(RowIs(a, &out, 1, "\xe2\x94\x82 a \xe2\x94\x82 b \xe2\x94\x82"),
              RowIs(a, &out, 3, "\xe2\x94\x82 1 \xe2\x94\x82 2 \xe2\x94\x82"));
  }
  {
    Array<SpanLine> out = Render(a, md);
    CHECK(SpanHas(&out.Data[1].Spans, "a", ATTR_BOLD, 0));
  }
  {
    Array<SpanLine> out = Render(a, md);
    u32 width = TextWidth(RowText(a, &out.Data[0]));
    CHECK(width == 9);
    for (u32 i = 0; i < out.Count; i++) CHECK(TextWidth(RowText(a, &out.Data[i])) == width);
  }
  {
    Array<SpanLine> out = Render(a, "| a | b |\n|:--|--:|\n| 1 | 2 |");
    CHECK(out.Count == 5);
  }
  {
    Array<SpanLine> out = Render(a, "| a | b |\n|---|---|\n| 1 |");
    u32 width = TextWidth(RowText(a, &out.Data[0]));
    for (u32 i = 0; i < out.Count; i++) CHECK(TextWidth(RowText(a, &out.Data[i])) == width);
  }
  {
    Array<SpanLine> out =
        Render(a, "| alpha beta | gamma delta |\n|---|---|\n| one two | three four |", 24);
    for (u32 i = 0; i < out.Count; i++) CHECK(TextWidth(RowText(a, &out.Data[i])) <= 24);
  }
  {
    Array<SpanLine> out = Render(a, md, 4);
    CHECK(AnyRowIs(a, &out, "| a | b |"));
  }
  {
    Array<SpanLine> out = Render(a, "a | b");
    CHECK_ALL(out.Count == 1, RowIs(a, &out, 0, "a | b"));
  }
  {
    Array<SpanLine> out = Render(a, "| a | b |\n|---|---|\n| `x` | y |");
    CHECK(SpanHas(&out.Data[3].Spans, "x", 0, Pal().Code));
  }
}

void TestParagraphs(Arena* a) {
  {
    Array<SpanLine> out = Render(a, "one\ntwo");
    CHECK_ALL(out.Count == 2, RowIs(a, &out, 0, "one"), RowIs(a, &out, 1, "two"));
  }
  {
    Array<SpanLine> out = Render(a, "**bold\nacross**");
    CHECK_ALL(out.Count == 2, SpanHas(&out.Data[0].Spans, "bold", ATTR_BOLD, 0),
              SpanHas(&out.Data[1].Spans, "across", ATTR_BOLD, 0));
  }
  {
    Array<SpanLine> out = Render(a, "one  \ntwo");
    CHECK_ALL(out.Count == 2, RowIs(a, &out, 0, "one"), RowIs(a, &out, 1, "two"));
  }
  {
    Array<SpanLine> out = Render(a, "a\n\n\n\nb");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 1, ""));
  }
  {
    Array<SpanLine> out = Render(a, "\n\na\n\n");
    CHECK_ALL(out.Count == 1, RowIs(a, &out, 0, "a"));
  }
  {
    Array<SpanLine> out = Render(a, "intro\n- item");
    CHECK_ALL(out.Count == 2, RowIs(a, &out, 0, "intro"), RowIs(a, &out, 1, "- item"));
  }
  {
    Array<SpanLine> out = Render(a, "<div>raw</div>");
    CHECK(RowIs(a, &out, 0, "<div>raw</div>"));
  }
  {
    Array<SpanLine> out = Render(a, "a\tb");
    CHECK(RowIs(a, &out, 0, "a   b"));
  }
  {
    Array<SpanLine> out = Render(a, "a\r\n\r\nb");
    CHECK_ALL(out.Count == 3, RowIs(a, &out, 0, "a"), RowIs(a, &out, 2, "b"));
  }
  {
    Array<SpanLine> out = Render(a, "");
    CHECK(out.Count == 0);
  }
}

void TestStreaming(Arena* a) {
  Array<SpanLine> closed = Render(a, "```\ncode\n```");
  {
    Array<SpanLine> out = Render(a, "```\ncode\n");
    CHECK_ALL(out.Count == closed.Count, RowIs(a, &out, 1, "  code"));
  }
  {
    Array<SpanLine> out = Render(a, "```\ncode\n`");
    CHECK_ALL(out.Count == closed.Count, RowIs(a, &out, 1, "  code"));
  }
  {
    Array<SpanLine> out = Render(a, "```\ncode\n``");
    CHECK_ALL(out.Count == closed.Count, RowIs(a, &out, 1, "  code"));
  }
  {
    Array<SpanLine> out = Render(a, "```\na\n```\n\nafter");
    CHECK(AnyRowIs(a, &out, "after"));
  }
  {
    Array<SpanLine> out = Render(a, "text ``");
    CHECK(RowIs(a, &out, 0, "text ``"));
  }
  {
    Array<SpanLine> whole = Render(a, "# Title\n\nSome text\n\n- one\n- two\n");
    Array<SpanLine> head = Render(a, "# Title\n\nSome text\n\n- one");
    CHECK(head.Count <= whole.Count);
    for (u32 i = 0; i < head.Count && i < whole.Count; i++)
      CHECK(StrView_Eq(RowText(a, &head.Data[i]), RowText(a, &whole.Data[i])));
  }
}

}

void TestMarkdown() {
  Arena a = ArenaCreate();
  TestInline(&a);
  TestHeadings(&a);
  TestCodeBlocks(&a);
  TestLists(&a);
  TestQuotes(&a);
  TestRules(&a);
  TestTables(&a);
  TestParagraphs(&a);
  TestStreaming(&a);
  ArenaRelease(&a);
}
