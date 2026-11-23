#pragma once
#include "base/arena.h"
#include "base/str.h"

// stdin is written before the wait loop, so a payload over the pipe buffer could
// block past timeout/abort. Windows pipe size; under macOS's 16 KiB default.
constexpr u32 EXEC_INPUT_PIPE_BYTES = 1u << 14;

struct ExecResult {
  i32 ExitCode;
  bool TimedOut;
  bool Spawned;
  StrView Output;
};

typedef bool (*ExecChunkFn)(void* user, StrView chunk);

// Security: Windows bare names resolve via PATH only, never the cwd.
ExecResult Exec_RunStreaming(Arena* arena, StrView command, u32 timeoutMs,
                             volatile i32* abort, ExecChunkFn onData,
                             void* user);

struct ExecArgvSink {
  void* User;
  ExecChunkFn OnStdout;
  ExecChunkFn OnStderr;
};

struct ExecArgvResult {
  i32 ExitCode;
  bool TimedOut;
  bool Spawned;
};

ExecArgvResult Exec_RunArgv(Arena* arena, StrView program, const StrView* args, u32 argCount,
                            u32 timeoutMs, volatile i32* abort, ExecArgvSink sink);

// Never inherits our stdin (the TUI holds the console raw); stderr goes to the
// null device so a hook's stdout stays clean. `outputLimit` caps memory only.
ExecResult Exec_RunShellInput(Arena* arena, StrView command, StrView input,
                              u32 timeoutMs, volatile i32* abort,
                              StrBuf* output = nullptr, u32 outputLimit = 0);

bool Exec_SpawnDetached(Arena* arena, StrView program, const StrView* args,
                        u32 argCount, StrView workingDirectory);
