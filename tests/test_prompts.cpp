#include "check.h"
#include "prompts/prompts.h"

namespace {

void TestEmbeddedText() {
  bool normalized = PROMPT_TEXT_COUNT > 0;
  for (u32 i = 0; i < PROMPT_TEXT_COUNT; i++) {
    StrView text = PROMPT_TEXTS[i].Text;
    normalized = normalized && text.Length > 0 && text.Data[text.Length - 1] != '\n' &&
                 !StrView_Contains(text, "\r");
  }
  CHECK(normalized);

  StrView before = {};
  StrView after = {};
  CHECK_ALL(Prompt_Split(PROMPT_SYSTEM, SVL("tools"), &before, &after),
            Prompt_Split(PROMPT_SKILLS, SVL("skills"), &before, &after),
            Prompt_Split(PROMPT_PROJECT_CONTEXT, SVL("files"), &before, &after),
            StrView_Contains(PROMPT_DREAM, "{{inbox}}"));
}

void TestExpansion(Arena* a) {
  PromptVar vars[] = {{SVL("name"), SVL("Clippi")}, {SVL("empty"), {}}};
  CHECK(StrView_EqCStr(
      Prompt_Expand(a, SVL("{{name}}:{{empty}}|{{unknown}}|{name}|{{name"), vars, 2),
      "Clippi:|{{unknown}}|{name}|{{name"));

  StrView before = {};
  StrView after = {};
  CHECK(Prompt_Split(SVL("a{{x}}b"), SVL("x"), &before, &after));
  CHECK_ALL(StrView_EqCStr(before, "a"), StrView_EqCStr(after, "b"),
            !Prompt_Split(SVL("a{{y}}b"), SVL("x"), &before, &after));

  StrView lines[4] = {};
  u32 count = Prompt_Lines(SVL("one\n\ntwo\nthree"), lines, 4);
  CHECK_ALL(count == 3, StrView_EqCStr(lines[0], "one"), StrView_EqCStr(lines[2], "three"));
}

}

void TestPrompts() {
  Arena arena = ArenaCreate();
  TestEmbeddedText();
  TestExpansion(&arena);
  ArenaRelease(&arena);
}
