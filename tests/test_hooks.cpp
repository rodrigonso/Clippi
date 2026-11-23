#include <stdio.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "app/hook_config.h"
#include "app/hook_gate.h"
#include "app/hook_runner.h"
#include "base/arena.h"
#include "base/config_file.h"
#include "base/str.h"
#include "base/thread.h"
#include "check.h"
#include "json/json.h"
#include "tests.h"
#include "tools/exec.h"
#include "tools/fs.h"

namespace {

StrView SelectOne(const HookConfig* config, const char* event, const char* subject) {
  const HookCommand* commands[4];
  u32 count = HookConfig_Select(config, SV(event), SV(subject), commands, 4);
  return count ? commands[0]->Command : StrView{"", 0};
}

u32 SelectCount(const HookConfig* config, const char* event, const char* subject) {
  const HookCommand* commands[8];
  return HookConfig_Select(config, SV(event), SV(subject), commands, 8);
}

void TestMatcher() {
  CHECK_ALL(HookMatcher_Accepts(StrView{"", 0}, SVL("read")),
            HookMatcher_Accepts(StrView{"", 0}, StrView{"", 0}));

  CHECK_ALL(HookMatcher_Accepts(SVL("read"), SVL("read")),
            !HookMatcher_Accepts(SVL("read"), SVL("write")),
            !HookMatcher_Accepts(SVL("read"), SVL("read_file")));

  CHECK_ALL(HookMatcher_Accepts(SVL("read|write|edit"), SVL("read")),
            HookMatcher_Accepts(SVL("read|write|edit"), SVL("write")),
            HookMatcher_Accepts(SVL("read|write|edit"), SVL("edit")),
            !HookMatcher_Accepts(SVL("read|write|edit"), SVL("grep")));

  CHECK_ALL(HookMatcher_Accepts(SVL("*"), SVL("read")),
            HookMatcher_Accepts(SVL("*"), SVL("anything")),
            HookMatcher_Accepts(SVL("read|*"), SVL("grep")));

  CHECK_ALL(HookMatcher_Accepts(SVL("read"), StrView{"", 0}),
            HookMatcher_Accepts(SVL("a||b"), StrView{"", 0}));
}

void TestParse() {
  Arena a = ArenaCreate();

  HookConfig config = {};
  CHECK(HookConfig_Parse(&a, SVL("{bad"), &config) == HookConfigLoadResult::Malformed);
  CHECK(HookConfig_Parse(&a, SVL("[]"), &config) == HookConfigLoadResult::Malformed);

  CHECK(HookConfig_Parse(&a, SVL("{\"hooks\":{}}"), &config) ==
        HookConfigLoadResult::Success);
  CHECK_ALL(config.CommandCount == 0, config.Events.Count == 0);

  StrView document = SVL(
      "{\"hooks\":{"
      "\"SessionStart\":[{\"hooks\":[{\"type\":\"command\","
      "\"command\":\"start.exe\",\"timeout\":10}]}],"
      "\"PreToolUse\":[{\"matcher\":\"read|write\",\"hooks\":[{\"type\":\"command\","
      "\"command\":\"tool.exe\",\"timeout\":3}]}],"
      "\"SessionEnd\":[{\"hooks\":[{\"type\":\"command\",\"command\":\"end.exe\"}]}]"
      "}}");
  CHECK(HookConfig_Parse(&a, document, &config) == HookConfigLoadResult::Success);
  CHECK_ALL(config.Events.Count == 3, config.CommandCount == 3);

  CHECK(StrView_Eq(SelectOne(&config, "SessionStart", ""), SVL("start.exe")));
  CHECK(StrView_Eq(SelectOne(&config, "SessionEnd", ""), SVL("end.exe")));

  CHECK(StrView_Eq(SelectOne(&config, "PreToolUse", "read"), SVL("tool.exe")));
  CHECK(StrView_Eq(SelectOne(&config, "PreToolUse", "write"), SVL("tool.exe")));
  CHECK(SelectCount(&config, "PreToolUse", "grep") == 0);

  CHECK(SelectCount(&config, "Stop", "") == 0);

  const HookCommand* selected[4];
  CHECK(HookConfig_Select(&config, SVL("SessionStart"), StrView{"", 0}, selected, 4) == 1);
  CHECK(selected[0]->TimeoutMs == 10000);
  CHECK(HookConfig_Select(&config, SVL("SessionEnd"), StrView{"", 0}, selected, 4) == 1);
  CHECK(selected[0]->TimeoutMs == HOOK_DEFAULT_TIMEOUT_MS);

  CHECK(HookConfig_Parse(
            &a,
            SVL("{\"hooks\":{\"Stop\":[{\"hooks\":[{\"type\":\"command\","
                "\"command\":\"x\",\"timeout\":0.0005}]}]}}"),
            &config) == HookConfigLoadResult::Success);
  CHECK(HookConfig_Select(&config, SVL("Stop"), StrView{"", 0}, selected, 4) == 1);
  CHECK(selected[0]->TimeoutMs >= 1);

  ArenaRelease(&a);
}

void TestParseTolerance() {
  Arena a = ArenaCreate();
  HookConfig config = {};

  CHECK(HookConfig_Parse(
            &a, SVL("{\"Stop\":[{\"hooks\":[{\"type\":\"command\",\"command\":\"s.exe\"}]}]}"),
            &config) == HookConfigLoadResult::Success);
  CHECK(StrView_Eq(SelectOne(&config, "Stop", ""), SVL("s.exe")));

  CHECK(HookConfig_Parse(
            &a,
            SVL("{\"hooks\":{\"Stop\":[{\"hooks\":["
                "{\"type\":\"webhook\",\"url\":\"http://x\"},"
                "{\"type\":\"command\"},"
                "{\"type\":\"command\",\"command\":\"good.exe\"}]}]}}"),
            &config) == HookConfigLoadResult::Success);
  CHECK_ALL(config.CommandCount == 1,
            StrView_Eq(SelectOne(&config, "Stop", ""), SVL("good.exe")));

  CHECK(HookConfig_Parse(
            &a,
            SVL("{\"hooks\":{\"Stop\":["
                "{\"hooks\":[{\"type\":\"command\",\"command\":\"a.exe\"},"
                "{\"type\":\"command\",\"command\":\"b.exe\"}]},"
                "{\"hooks\":[{\"type\":\"command\",\"command\":\"c.exe\"}]}]}}"),
            &config) == HookConfigLoadResult::Success);
  CHECK(SelectCount(&config, "Stop", "") == 3);

  ArenaRelease(&a);
}

void TestLoad() {
  Arena a = ArenaCreate();
  const char* dir = "clippi_hooks_test";
  const char* path = "clippi_hooks_test/hooks.json";
  remove(path);
#if defined(_WIN32)
  _rmdir(dir);
#else
  rmdir(dir);
#endif

  HookConfig config = {};
  CHECK(HookConfig_Load(&a, SV(path), &config) == HookConfigLoadResult::Missing);
  CHECK(config.CommandCount == 0);

  CHECK(ConfigFile_WriteAtomic(&a, SV(path), SVL("{oops")));
  CHECK(HookConfig_Load(&a, SV(path), &config) == HookConfigLoadResult::Malformed);

  CHECK(ConfigFile_WriteAtomic(
      &a, SV(path),
      SVL("{\"hooks\":{\"SessionStart\":[{\"hooks\":[{\"type\":\"command\","
          "\"command\":\"echo hi\"}]}]}}")));
  CHECK(HookConfig_Load(&a, SV(path), &config) == HookConfigLoadResult::Success);
  CHECK(StrView_Eq(SelectOne(&config, "SessionStart", ""), SVL("echo hi")));

  remove(path);
#if defined(_WIN32)
  _rmdir(dir);
#else
  rmdir(dir);
#endif
  ArenaRelease(&a);
}

void TestPayload() {
  Arena a = ArenaCreate();

  HookSessionInfo session = {};
  session.SessionId = SVL("0123456789abcdefghijkl");
  session.WorkingDirectory = SVL("C:\\work\\repo");
  session.TranscriptPath = SVL("C:\\home\\.clippi\\sessions\\v2\\repo-1\\s.jsonl");
  session.Interactive = true;

  HookToolInfo tool = {};
  tool.Name = SVL("read");
  tool.CallId = SVL("call_42");
  tool.InputJson = SVL("{\"path\":\"a.txt\"}");

  StrBuf payload = StrBuf_Make(&a, 256);
  HookRunner_BuildPayload(&payload, HookEvent::PreToolUse, &session, &tool);
  StrView text = StrBuf_View(&payload);
  CHECK_ALL(StrView_Contains(text, "\"hook_event_name\":\"PreToolUse\""),
            StrView_Contains(text, "\"session_id\":\"0123456789abcdefghijkl\""),
            StrView_Contains(text, "\"tool_name\":\"read\""),
            StrView_Contains(text, "\"tool_call_id\":\"call_42\""),
            StrView_Contains(text, "\"tool_input\":{\"path\":\"a.txt\"}"),
            StrView_Contains(text, "\"interactive\":true"),
            !StrView_Contains(text, "tool_input_omitted"),
            StrView_Contains(text, "C:\\\\work\\\\repo"),
            StrView_Contains(text, "s.jsonl"));

  HookToolInfo omitted = {};
  omitted.Name = SVL("write");
  omitted.InputOmitted = true;
  StrBuf dropped = StrBuf_Make(&a, 256);
  HookRunner_BuildPayload(&dropped, HookEvent::PreToolUse, &session, &omitted);
  CHECK_ALL(StrView_Contains(StrBuf_View(&dropped), "\"tool_input_omitted\":true"),
            !StrView_Contains(StrBuf_View(&dropped), "\"tool_input\":"));

  StrBuf post = StrBuf_Make(&a, 256);
  HookRunner_BuildPayload(&post, HookEvent::PostToolUse, &session, &tool);
  CHECK(!StrView_Contains(StrBuf_View(&post), "interactive"));

  StrBuf sessionStart = StrBuf_Make(&a, 256);
  HookRunner_BuildPayload(&sessionStart, HookEvent::SessionStart, &session, nullptr);
  CHECK_ALL(StrView_Contains(StrBuf_View(&sessionStart),
                             "\"hook_event_name\":\"SessionStart\""),
            !StrView_Contains(StrBuf_View(&sessionStart), "tool_name"));

  HookSessionInfo empty = {};
  StrBuf bare = StrBuf_Make(&a, 128);
  HookRunner_BuildPayload(&bare, HookEvent::SessionEnd, &empty, nullptr);
  CHECK(StrView_Eq(StrBuf_View(&bare),
                   SVL("{\"hook_event_name\":\"SessionEnd\"}")));

  ArenaRelease(&a);
}

void TestRunnerLifecycle() {
  Arena a = ArenaCreate();

  HookConfig empty = {};
  HookConfig_Parse(&a, SVL("{\"hooks\":{}}"), &empty);
  HookSessionInfo session = {};
  CHECK(HookRunner_Create(&a, &empty, &session) == nullptr);

  HookRunner_Dispatch(nullptr, HookEvent::Stop, StrView{"", 0});
  HookRunner_Shutdown(nullptr, HookEvent::SessionEnd);

  CHECK(StrView_Eq(HookEvent_Name(HookEvent::UserPromptSubmit), SVL("UserPromptSubmit")));
  CHECK(StrView_Eq(HookEvent_Name(HookEvent::PostToolUse), SVL("PostToolUse")));
  CHECK_ALL(StrView_Eq(HookEvent_Name(HookEvent::Stop), SVL("Stop")),
            StrView_Eq(HookEvent_Name(HookEvent::StopFailure), SVL("StopFailure")));

  ArenaRelease(&a);
}

StrView SelfCommand(Arena* a, const char* subcommand, StrView argument) {
  StrView exe = Fs_RealPath(a, SV(g_test_executable));
  StrBuf out = StrBuf_Make(a, exe.Length + argument.Length + 32);
  StrBuf_AppendChar(&out, '"');
  StrBuf_Append(&out, exe);
  StrBuf_AppendCStr(&out, "\" ");
  StrBuf_AppendCStr(&out, subcommand);
  StrBuf_AppendCStr(&out, " \"");
  StrBuf_Append(&out, argument);
  StrBuf_AppendChar(&out, '"');
  return StrBuf_View(&out);
}

StrView Slurp(Arena* a, const char* path) {
  StrView content = {};
  return ConfigFile_ReadAll(a, SV(path), &content) == ConfigFileReadResult::Success
             ? content
             : StrView{"", 0};
}

u32 CountLines(StrView text) {
  u32 count = 0;
  for (u32 i = 0; i < text.Length; i++)
    if (text.Data[i] == '\n') count++;
  return count;
}

void TestRunnerDelivery() {
  Arena a = ArenaCreate();
  const char* sinkPath = "clippi_hooks_live.txt";
  remove(sinkPath);

  StrBuf document = StrBuf_Make(&a, 512);
  StrBuf_AppendCStr(&document, "{\"hooks\":{\"PreToolUse\":[{\"hooks\":[{\"type\":\"command\",\"command\":");
  Json_WriteString(&document, SelfCommand(&a, "--sink-stdin", SV(sinkPath)));
  StrBuf_AppendCStr(&document, "}]}],\"SessionEnd\":[{\"hooks\":[{\"type\":\"command\",\"command\":");
  Json_WriteString(&document, SelfCommand(&a, "--sink-stdin", SV(sinkPath)));
  StrBuf_AppendCStr(&document, "}]}]}}");

  HookConfig config = {};
  CHECK(HookConfig_Parse(&a, StrBuf_View(&document), &config) ==
        HookConfigLoadResult::Success);

  HookSessionInfo session = {};
  session.SessionId = SVL("live-session-id");
  session.WorkingDirectory = SVL("live-cwd");
  HookRunner* runner = HookRunner_Create(&a, &config, &session);
  CHECK(runner != nullptr);

  HookRunner_Dispatch(runner, HookEvent::PreToolUse, SVL("read"));
  HookRunner_Shutdown(runner, HookEvent::SessionEnd);

  StrView written = Slurp(&a, sinkPath);
  CHECK_ALL(StrView_Contains(written, "\"hook_event_name\":\"PreToolUse\""),
            StrView_Contains(written, "\"tool_name\":\"read\""),
            StrView_Contains(written, "\"session_id\":\"live-session-id\""),
            StrView_Contains(written, "\"hook_event_name\":\"SessionEnd\""));

  remove(sinkPath);
  ArenaRelease(&a);
}

void TestRunnerShutdownIsBounded() {
  Arena a = ArenaCreate();
  const char* sinkPath = "clippi_hooks_bounded.txt";
  remove(sinkPath);

  StrBuf document = StrBuf_Make(&a, 512);
  StrBuf_AppendCStr(&document, "{\"hooks\":{\"Stop\":[{\"hooks\":[{\"type\":\"command\",\"command\":");
  Json_WriteString(&document, SelfCommand(&a, "--hang", SVL("30000")));
  StrBuf_AppendCStr(&document, ",\"timeout\":600}]}],\"SessionEnd\":[{\"hooks\":[{\"type\":\"command\",\"command\":");
  Json_WriteString(&document, SelfCommand(&a, "--sink-stdin", SV(sinkPath)));
  StrBuf_AppendCStr(&document, "}]}]}}");

  HookConfig config = {};
  CHECK(HookConfig_Parse(&a, StrBuf_View(&document), &config) ==
        HookConfigLoadResult::Success);

  HookSessionInfo session = {};
  session.SessionId = SVL("bounded");
  HookRunner* runner = HookRunner_Create(&a, &config, &session);
  CHECK(runner != nullptr);

  // Far more than the queue holds, each nominally a 600 s hook.
  for (u32 i = 0; i < 200; i++)
    HookRunner_Dispatch(runner, HookEvent::Stop, StrView{"", 0});

  u64 start = MonotonicMillis();
  HookRunner_Shutdown(runner, HookEvent::SessionEnd);
  u64 elapsed = MonotonicMillis() - start;

  CHECK(elapsed < 20000);
  CHECK(StrView_Contains(Slurp(&a, sinkPath), "\"hook_event_name\":\"SessionEnd\""));

  remove(sinkPath);
  ArenaRelease(&a);
}

void TestExecInputTimeout() {
  Arena a = ArenaCreate();

  u64 start = MonotonicMillis();
  // A leading quoted path doubles as the `cmd /c` re-wrapping regression.
  StrView hangCommand = SelfCommand(&a, "--hang", SVL("30000"));
  ExecResult result =
      Exec_RunShellInput(&a, hangCommand, SVL("{\"ignored\":true}"), 500, nullptr);
  u64 elapsed = MonotonicMillis() - start;
  CHECK_ALL(result.Spawned, result.TimedOut, elapsed < 10000);

  StrBuf huge = StrBuf_Make(&a, (1u << 16) + 16);
  for (u32 i = 0; i < (1u << 16); i++) StrBuf_AppendChar(&huge, 'x');
  ExecResult refused = Exec_RunShellInput(&a, SVL("cmd /c exit 0"), StrBuf_View(&huge),
                                          1000, nullptr);
  CHECK(!refused.Spawned);

  ArenaRelease(&a);
}

void TestExecInputCapture() {
  Arena a = ArenaCreate();

  const int kLines = 4000;  // ~260 KB, several times any default pipe buffer
  StrView floodCommand = SelfCommand(&a, "--flood-stdout", SVL("4000"));
  StrBuf captured = StrBuf_Make(&a, 1024);
  u64 start = MonotonicMillis();
  ExecResult result = Exec_RunShellInput(&a, floodCommand, SVL("{\"unread\":true}"), 30000,
                                         nullptr, &captured);
  u64 elapsed = MonotonicMillis() - start;
  CHECK_ALL(result.Spawned, !result.TimedOut, result.ExitCode == 0, elapsed < 30000);

  // Count newlines, not bytes: Windows CRLF-translates the child's text-mode stdout.
  u32 newlines = 0;
  for (u32 i = 0; i < result.Output.Length; i++)
    if (result.Output.Data[i] == '\n') newlines++;
  CHECK(newlines == (u32)kLines);

  StrBuf clipped = StrBuf_Make(&a, 1024);
  ExecResult limited = Exec_RunShellInput(&a, floodCommand, SVL("{\"unread\":true}"), 30000,
                                          nullptr, &clipped, 8192);
  CHECK_ALL(limited.Spawned, !limited.TimedOut, limited.ExitCode == 0,
            limited.Output.Length == 8192);

  ArenaRelease(&a);
}
void TestRunnerSessionSwitch() {
  Arena a = ArenaCreate();
  const char* sinkPath = "clippi_hooks_switch.txt";
  remove(sinkPath);

  StrBuf document = StrBuf_Make(&a, 512);
  StrBuf_AppendCStr(&document,
                    "{\"hooks\":{\"SessionEnd\":[{\"hooks\":[{\"type\":\"command\",\"command\":");
  Json_WriteString(&document, SelfCommand(&a, "--sink-stdin", SV(sinkPath)));
  StrBuf_AppendCStr(&document, "}]}]}}");

  HookConfig config = {};
  CHECK(HookConfig_Parse(&a, StrBuf_View(&document), &config) ==
        HookConfigLoadResult::Success);

  HookSessionInfo first = {};
  first.SessionId = SVL("first-session");
  first.WorkingDirectory = SVL("C:\\Users\\dev\\projects\\clippi");
  first.TranscriptPath = SVL("C:\\Users\\dev\\.clippi\\sessions\\v2\\clippi-c838f21a\\first.jsonl");
  HookRunner* runner = HookRunner_Create(&a, &config, &first);
  CHECK(runner != nullptr);

  HookSessionInfo second = {};
  second.SessionId = SVL("second-session");
  second.WorkingDirectory = SVL("C:\\Users\\dev\\projects\\clippi");
  second.TranscriptPath =
      SVL("C:\\Users\\dev\\.clippi\\sessions\\v2\\clippi-c838f21a\\second.jsonl");
  for (u32 i = 0; i < 20000; i++) HookRunner_SetSession(runner, &second);

  StrBuf overlong = StrBuf_Make(&a, 4096);
  for (u32 i = 0; i < 3000; i++) StrBuf_AppendChar(&overlong, 'p');
  HookSessionInfo third = second;
  third.TranscriptPath = StrBuf_View(&overlong);
  HookRunner_SetSession(runner, &third);

  HookRunner_Shutdown(runner, HookEvent::SessionEnd);

  StrView written = Slurp(&a, sinkPath);
  CHECK_ALL(StrView_Contains(written, "\"session_id\":\"second-session\""),
            !StrView_Contains(written, "first-session"),
            !StrView_Contains(written, "transcript_path"));

  remove(sinkPath);
  ArenaRelease(&a);
}

void TestSelectionIsNotCapped() {
  Arena a = ArenaCreate();
  constexpr u32 kCommands = 12;
  const char* sinkPath = "clippi_hooks_fanout.txt";
  remove(sinkPath);

  StrView command = SelfCommand(&a, "--sink-stdin", SV(sinkPath));
  StrBuf document = StrBuf_Make(&a, 4096);
  StrBuf_AppendCStr(&document, "{\"hooks\":{\"SessionEnd\":[{\"hooks\":[");
  for (u32 i = 0; i < kCommands; i++) {
    if (i) StrBuf_AppendChar(&document, ',');
    StrBuf_AppendCStr(&document, "{\"type\":\"command\",\"command\":");
    Json_WriteString(&document, command);
    StrBuf_AppendChar(&document, '}');
  }
  StrBuf_AppendCStr(&document, "]}]}}");

  HookConfig config = {};
  CHECK(HookConfig_Parse(&a, StrBuf_View(&document), &config) ==
        HookConfigLoadResult::Success);
  CHECK(config.CommandCount == kCommands);

  const HookCommand* selected[kCommands];
  CHECK(HookConfig_Select(&config, SVL("SessionEnd"), StrView{"", 0}, selected, kCommands) ==
        kCommands);

  HookSessionInfo session = {};
  session.SessionId = SVL("fanout");
  HookRunner* runner = HookRunner_Create(&a, &config, &session);
  CHECK(runner != nullptr);
  HookRunner_Shutdown(runner, HookEvent::SessionEnd);

  CHECK(CountLines(Slurp(&a, sinkPath)) == kCommands);

  remove(sinkPath);
  ArenaRelease(&a);
}

StrView GateDocument(Arena* a, const StrView* commands, u32 count, const char* matcher) {
  StrBuf out = StrBuf_Make(a, 1024);
  StrBuf_AppendCStr(&out, "{\"hooks\":{\"PreToolUse\":[{");
  if (matcher) {
    StrBuf_AppendCStr(&out, "\"matcher\":");
    Json_WriteString(&out, SV(matcher));
    StrBuf_AppendChar(&out, ',');
  }
  StrBuf_AppendCStr(&out, "\"hooks\":[");
  for (u32 i = 0; i < count; i++) {
    if (i) StrBuf_AppendChar(&out, ',');
    StrBuf_AppendCStr(&out, "{\"type\":\"command\",\"command\":");
    Json_WriteString(&out, commands[i]);
    StrBuf_AppendChar(&out, '}');
  }
  StrBuf_AppendCStr(&out, "]}]}}");
  return StrBuf_View(&out);
}

HookGate* MakeGate(Arena* a, HookConfig* config, StrView document) {
  CHECK(HookConfig_Parse(a, document, config) == HookConfigLoadResult::Success);
  HookSessionInfo session = {};
  session.SessionId = SVL("gate-session");
  session.WorkingDirectory = SVL("gate-cwd");
  session.Interactive = true;
  return HookGate_Create(a, config, &session, nullptr);
}

AgentToolGateDecision Decide(Arena* a, ExecResult result, StrView output) {
  result.Output = output;
  AgentToolGateDecision decision = {};
  HookGate_Decide(a, &result, &decision);
  return decision;
}

void TestGateDecision() {
  Arena a = ArenaCreate();
  AgentToolGateDecision decision = {};

  ExecResult ok = {};
  ok.Spawned = true;
  ExecResult failed = ok;
  failed.ExitCode = 3;

  decision = Decide(&a, ok, StrView{"", 0});
  CHECK(!decision.Block);

  decision = Decide(&a, failed, SVL("  denied by policy\n"));
  CHECK_ALL(decision.Block, StrView_EqCStr(decision.Reason, "denied by policy"),
            !decision.Terminate);

  decision = Decide(&a, failed, SVL("{\"block\":false}"));
  CHECK(!decision.Block);
  decision = Decide(&a, ok, SVL("{\"block\":true,\"reason\":\"no\",\"terminate\":true}"));
  CHECK_ALL(decision.Block, StrView_EqCStr(decision.Reason, "no"), decision.Terminate);

  decision = Decide(&a, ok, SVL("{\"block\":true}"));
  CHECK_ALL(decision.Block, decision.Reason.Length == 0);

  decision = Decide(&a, ok, SVL("{\"block\":false,\"terminate\":true}"));
  CHECK_ALL(!decision.Block, !decision.Terminate);

  decision = Decide(&a, ok, SVL("[1,2,3]"));
  CHECK(!decision.Block);
  decision = Decide(&a, failed, SVL("{not json"));
  CHECK(decision.Block);

  decision = Decide(&a, failed, SVL("{\"decision\":\"block\",\"reason\":\"nope\"}"));
  CHECK(decision.Block);
  decision = Decide(&a, failed, SVL("{\"block\":\"true\"}"));
  CHECK(decision.Block);
  decision = Decide(&a, failed, SVL("{\"hookSpecificOutput\":{\"permissionDecision\":\"deny\"}}"));
  CHECK(decision.Block);
  decision = Decide(&a, ok, SVL("{\"unrelated\":1}"));
  CHECK(!decision.Block);

  StrBuf flood = StrBuf_Make(&a, (1u << 16) + 64);
  StrBuf_AppendCStr(&flood, "{\"block\":true,\"reason\":\"");
  while (flood.Length < (1u << 16) + 8) StrBuf_AppendChar(&flood, 'x');
  StrBuf_AppendCStr(&flood, "\"}");
  decision = Decide(&a, ok, StrBuf_View(&flood));
  CHECK_ALL(decision.Block, StrView_Contains(decision.Reason, "too much output"));

  StrBuf chatty = StrBuf_Make(&a, 16384);
  StrBuf_AppendCStr(&chatty, "{\"block\":true,\"reason\":\"");
  for (u32 i = 0; i < 9000; i++) StrBuf_AppendChar(&chatty, 'y');
  StrBuf_AppendCStr(&chatty, "\"}");
  decision = Decide(&a, ok, StrBuf_View(&chatty));
  CHECK_ALL(decision.Block, decision.Reason.Length == 4096);

  ExecResult unspawned = {};
  unspawned.ExitCode = -1;
  decision = Decide(&a, unspawned, SVL("failed to start process"));
  CHECK_ALL(decision.Block, StrView_Contains(decision.Reason, "failed to start process"));

  ExecResult timedOut = ok;
  timedOut.TimedOut = true;
  decision = Decide(&a, timedOut, SVL("{\"block\":false}"));
  CHECK_ALL(decision.Block, StrView_Contains(decision.Reason, "timed out"));

  HookGate_Decide(&a, nullptr, &decision);
  CHECK(decision.Block);

  ArenaRelease(&a);
}

// Repeats to catch a rare POSIX elapsed-time underflow; keep both the count and the sleep.
void TestExecInputTimeoutIsNotSpurious() {
  Arena a = ArenaCreate();

  StrView briefCommand = SelfCommand(&a, "--hang", SVL("200"));
  bool everTimedOut = false;
  bool everFailed = false;
  for (u32 i = 0; i < 10; i++) {
    StrBuf captured = StrBuf_Make(&a, 64);
    ExecResult result =
        Exec_RunShellInput(&a, briefCommand, SVL("{}"), 60000, nullptr, &captured);
    if (result.TimedOut) everTimedOut = true;
    if (!result.Spawned || result.ExitCode != 0) everFailed = true;
  }
  CHECK_ALL(!everTimedOut, !everFailed);

  ArenaRelease(&a);
}
void TestGateDelivery() {
  Arena a = ArenaCreate();
  const char* sinkPath = "clippi_gate_live.txt";
  ToolArgs args = {};
  args.Json = SVL("{\"path\":\"a.txt\"}");
  AgentToolGateDecision decision = {};

  {
    remove(sinkPath);
    StrView commands[1] = {SelfCommand(&a, "--deny-stdin", SV(sinkPath))};
    HookConfig config = {};
    HookGate* gate = MakeGate(&a, &config, GateDocument(&a, commands, 1, nullptr));
    CHECK(gate != nullptr);
    HookGate_BeforeTool(gate, &a, SVL("read"), SVL("call-1"), &args, &decision);
    StrView seen = Slurp(&a, sinkPath);
    CHECK_ALL(decision.Block, StrView_EqCStr(decision.Reason, "denied by policy"),
              StrView_Contains(seen, "\"hook_event_name\":\"PreToolUse\""),
              StrView_Contains(seen, "\"tool_name\":\"read\""),
              StrView_Contains(seen, "\"tool_call_id\":\"call-1\""),
              StrView_Contains(seen, "\"tool_input\":{\"path\":\"a.txt\"}"),
              StrView_Contains(seen, "\"interactive\":true"));
  }

  {
    remove(sinkPath);
    StrView commands[1] = {SelfCommand(&a, "--deny-json-stdin", SV(sinkPath))};
    HookConfig config = {};
    HookGate* gate = MakeGate(&a, &config, GateDocument(&a, commands, 1, nullptr));
    HookGate_BeforeTool(gate, &a, SVL("read"), SVL("call-2"), &args, &decision);
    CHECK_ALL(decision.Block, StrView_EqCStr(decision.Reason, "structured no"),
              decision.Terminate);
  }

  {
    remove(sinkPath);
    StrView commands[1] = {SelfCommand(&a, "--allow-json-stdin", SV(sinkPath))};
    HookConfig config = {};
    HookGate* gate = MakeGate(&a, &config, GateDocument(&a, commands, 1, nullptr));
    HookGate_BeforeTool(gate, &a, SVL("read"), SVL("call-3"), &args, &decision);
    CHECK(!decision.Block);
  }

  {
    remove(sinkPath);
    StrView commands[2] = {SelfCommand(&a, "--deny-stdin", SV(sinkPath)),
                           SelfCommand(&a, "--sink-stdin", SV(sinkPath))};
    HookConfig config = {};
    HookGate* gate = MakeGate(&a, &config, GateDocument(&a, commands, 2, nullptr));
    HookGate_BeforeTool(gate, &a, SVL("read"), SVL("call-4"), &args, &decision);
    CHECK_ALL(decision.Block, CountLines(Slurp(&a, sinkPath)) == 1);
  }

  {
    remove(sinkPath);
    StrView commands[1] = {SelfCommand(&a, "--deny-stdin", SV(sinkPath))};
    HookConfig config = {};
    HookGate* gate = MakeGate(&a, &config, GateDocument(&a, commands, 1, "shell|write"));
    CHECK(gate != nullptr);
    HookGate_BeforeTool(gate, &a, SVL("read"), SVL("call-5"), &args, &decision);
    CHECK_ALL(!decision.Block, CountLines(Slurp(&a, sinkPath)) == 0);
    HookGate_BeforeTool(gate, &a, SVL("shell"), SVL("call-6"), &args, &decision);
    CHECK_ALL(decision.Block, CountLines(Slurp(&a, sinkPath)) == 1);
  }

  {
    HookConfig config = {};
    StrBuf document = StrBuf_Make(&a, 512);
    StrBuf_AppendCStr(&document,
                      "{\"hooks\":{\"PreToolUse\":[{\"hooks\":[{\"type\":\"command\","
                      "\"timeout\":1,\"command\":");
    Json_WriteString(&document, SelfCommand(&a, "--hang", SVL("4000")));
    StrBuf_AppendCStr(&document, "}]}]}}");
    HookGate* gate = MakeGate(&a, &config, StrBuf_View(&document));
    HookGate_BeforeTool(gate, &a, SVL("read"), SVL("call-7"), &args, &decision);
    CHECK_ALL(decision.Block, StrView_Contains(decision.Reason, "timed out"));
  }

  {
    HookConfig config = {};
    CHECK(HookConfig_Parse(
              &a, SVL("{\"hooks\":{\"PostToolUse\":[{\"hooks\":[{\"type\":\"command\","
                      "\"command\":\"echo\"}]}]}}"),
              &config) == HookConfigLoadResult::Success);
    HookSessionInfo session = {};
    CHECK(HookGate_Create(&a, &config, &session, nullptr) == nullptr);
    HookGate_BeforeTool(nullptr, &a, SVL("read"), SVL("call-8"), &args, &decision);
    CHECK(!decision.Block);
  }

  remove(sinkPath);
  ArenaRelease(&a);
}

}

void TestHooks() {
  TestMatcher();
  TestParse();
  TestParseTolerance();
  TestLoad();
  TestPayload();
  TestRunnerLifecycle();
  TestRunnerDelivery();
  TestRunnerSessionSwitch();
  TestSelectionIsNotCapped();
  TestRunnerShutdownIsBounded();
  TestExecInputTimeout();
  TestExecInputCapture();
  TestExecInputTimeoutIsNotSpurious();
  TestGateDecision();
  TestGateDelivery();
}
