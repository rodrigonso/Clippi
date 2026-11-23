# Lifecycle hooks

Clippi reports lifecycle transitions to external commands so an editor or
supervisor can follow a run it did not start, and lets one of those commands
decide whether a tool call is allowed to run at all. Hooks are configured in
`~/.clippi/hooks.json` — deliberately **not** `settings.json`, which is rewritten
from its own known keys whenever a setting changes and would drop anything it
does not model.

```json
{
  "hooks": {
    "SessionStart": [
      { "hooks": [{ "type": "command", "command": "notify.exe", "timeout": 10 }] }
    ],
    "PreToolUse": [
      { "matcher": "read|write", "hooks": [{ "type": "command", "command": "audit.exe" }] }
    ]
  }
}
```

`matcher` is an optional `|`-separated alternation of exact tool names; `*`,
empty, or absent accepts everything. It is matched against the tool name, so it
only narrows `PreToolUse` and `PostToolUse` — a matcher on any other event is
ignored rather than silencing it. `timeout` is in seconds and defaults to 10.
Entries of an unrecognized `type`, and entries with no `command`, are skipped
rather than invalidating the file — one bad entry never disables the rest. A
missing `hooks.json` simply disables hooks; one that cannot be read or parsed
disables them too, but says so on stderr rather than failing silently.

| Event | Fires |
| --- | --- |
| `SessionStart` | once the session is durable, before the first prompt |
| `UserPromptSubmit` | each prompt submitted to the agent |
| `PreToolUse` | before a tool call runs, and decides whether it runs at all |
| `PostToolUse` | after a tool call completes |
| `Stop` | when an agent run finishes normally |
| `StopFailure` | when a run ends in an error; a user cancel reports `Stop` instead |
| `SessionEnd` | at exit |

Each command runs through the platform shell and receives a JSON payload on
**stdin**:

```json
{
  "hook_event_name": "PreToolUse",
  "session_id": "DuPy5yQe3amJZd0ZKOaUlw",
  "cwd": "C:\\work\\repo",
  "transcript_path": "C:\\Users\\me\\.clippi\\sessions\\v2\\repo-c838...\\DuPy....jsonl",
  "tool_name": "read",
  "tool_call_id": "call_01",
  "tool_input": { "path": "src/main.cpp" },
  "interactive": true
}
```

On Windows the platform shell is the system `cmd.exe`, which resolves a bare
program name through `PATH` only, never the working directory; name a script in
the project as `.\notify.bat`. The `shell` tool follows the same rule.

`transcript_path` is reported directly so a consumer never has to re-derive the
project key. `tool_name`, `tool_call_id`, and `tool_input` are present only for
the tool events; `tool_input` is the canonical, already-validated arguments as a
nested object, not a string. If the arguments would push the payload past the
16 KiB stdin bound they are dropped whole and `"tool_input_omitted": true`
appears instead — half a JSON object is not a JSON object. `interactive` is
`true` when a user is present to answer, and appears only on `PreToolUse`.

The child never inherits Clippi's stdin — the TUI holds the console in raw mode.
For every event other than `PreToolUse` its stdout, stderr, and exit code are
ignored: a hook is a one-way status report, and a broken one must not become an
agent failure.

Dispatch for those events is queued onto a worker thread, so a slow hook cannot
stall the agent loop or a TUI redraw, and events are dropped rather than
blocking if a burst outruns the queue. Every run-scoped event is dispatched from
the harness's own event stream, so hooks observe them in the order the agent
produced them — `UserPromptSubmit` before the tool events it caused, and `Stop`
after the last of them. All commands registered for an event run in
configuration order, with no cap on how many. On exit the backlog gets a bounded
grace period to finish before anything still outstanding is cancelled, and
`SessionEnd` is then reported synchronously so it is delivered even when
everything else was dropped. With no hooks configured no thread is started and
nothing is spawned, so the default footprint is unchanged. `--no-session` keeps
everything in memory and has no transcript to point at, so it reports none of
these events.

Switching sessions with `/clear` or `/resume` re-announces `SessionStart` with
the new session ID, transcript path, and working directory, so a consumer never
keeps tailing a log that stopped being written.

## The `PreToolUse` gate

`PreToolUse` is the one event that is a question rather than a report. It runs
**synchronously**, on the agent's own thread, during the clearance phase — every
call in a batch is asked before any of them starts — and its answer decides
whether the tool runs.

A command answers in one of two ways, checked in this order:

1. **A JSON object on stdout carrying a boolean `block`.**
   `{"block": true, "reason": "...", "terminate": true}`. `reason` becomes the
   tool's error result verbatim, with no prefix; an empty or absent one falls
   back to `Tool execution was blocked`. `terminate` ends the run, but only if
   *every* call in the batch sets it. `terminate` without `block` is ignored.
2. **The exit code.** `0` allows; anything else blocks, with whatever the command
   printed to stdout as the reason.

A structured verdict outranks the exit code in both directions, so a hook can
allow while still reporting a non-zero status. That only applies to an object
with a real boolean `block`, though: `{"block": "true"}`, `{"decision": "deny"}`,
or any other schema is *not* a verdict, and falls through to the exit code rather
than being read as consent. Write the reason to **stdout**, not stderr: stderr
goes to the null device so a hook's diagnostics can never corrupt the decision
channel.

The gate **fails closed**. A command that cannot be spawned, outlives its
timeout, is cancelled, answers unintelligibly, or floods stdout with more than
64 KiB blocks the call and says why. A gate that fails open is not a gate — which
is also why it stays active under `--no-session`, unlike the reporting events.

Commands run in configuration order and the **first block wins**; the rest are
never spawned. `matcher` narrows the gate by tool name in the usual way, so a
tool no command matches costs nothing. Every gated call pays one process spawn,
bounded by that command's `timeout`.

The gate is independent of Clippi's built-in [permissions](PERMISSIONS.md) and
can still block a call they admit. A minimal policy:

```sh
#!/bin/sh
# Block `shell` calls that look destructive; allow everything else.
payload=$(cat)
case "$payload" in
  *'rm -rf'*|*sudo*) echo '{"block":true,"reason":"blocked by local policy"}' ;;
  *)                 echo '{"block":false}' ;;
esac
```
