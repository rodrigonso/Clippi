# Architecture

## Layout

A runnable Clippi agent: the agent loop + an OpenAI Responses API provider
(HTTP/SSE) + `read`, `write`, `edit`, `ls`, `find`, `grep`,
`shell`, and `ask_user` tools (and `remember` with the memory wiki) + a
streaming REPL.

```
src/
  base/    Arena (cross-platform reserve/commit), Array<T>, UTF-8 Str/StrBuf,
           FFI fastfail, peak-RSS reporter   (no C++ std)
  json/    arena DOM parser + escaping writer (hand-rolled, no deps)
  crypto/  base64, secure random (no deps)
  llm/     data model, OpenAI Responses protocol
           (request build + SSE parse), retry policy, model registry
  net/     streaming HTTP + non-streaming fetch over libcurl
  auth/    credential store (api keys, refreshable OAuth tokens) +
           resolution/caching + OAuth PKCE for OpenRouter and ChatGPT
  session/ append-only v2 message tree, reducer, memory + JSONL repositories
  agent/   incremental AgentHarness, in-memory queues, hooks, events, compaction
  tools/   read/write/edit/ls/find/grep/shell/ask_user, direct/shell process
           exec, in-process search, fs/glob/diff/truncate/filelock infra
  term/    full-screen TUI: raw-mode terminal, key decoder, differential
           renderer, markdown, transcript model, multi-line editor, controller
  app/     CLI entry + durable session startup + TUI/plain REPL + credential
           resolution + external lifecycle hooks (hooks.json config + dispatch
           runner)
tests/     single CLI test harness (suite selection via args; offline by default)
```

## Design notes

- **No C++ standard library**, no exceptions, no RTTI. Types and conventions
  are `Arena`, `Array<T>`, PascalCase fields, and the `FFI(cond)` fastfail.
  The CLI uses the C runtime, platform APIs, and the
  C APIs of libcurl and PCRE2.
- **Append-only durable sessions.** Session v2 entries form a branchable,
  message-oriented tree containing only model-visible state, compactions,
  names, labels, and the selected leaf. Active turns and message queues remain
  in memory, matching pi. A crash can leave an assistant tool call without a
  result; provider replay supplies a transient `No result provided` error
  without persisting it or rerunning the side effect. JSONL is the default
  backend, while `--no-session` uses the same contracts entirely in memory.
- **Single-threaded loop, parallel tool batches.** A pluggable `Provider`
  streams one assistant turn; a turn's tool calls run **concurrently by default** (each
  on a small worker pool, isolated in its own arena), matching pi. Transient
  completion events are published in actual finish order, while final results
  are appended in original call order. Provider-request and whole-assistant-turn
  retry layers are separately configurable and default to off. Tools receive centrally
  parsed, schema-validated, coercion-normalized arguments; unsupported
  validation keywords fail harness initialization. The `shell` tool also emits
  bounded, replace-in-place partial output snapshots. These live updates never
  enter model context or the session log, so the source-ordered durable result
  remains authoritative for replay. Set the agent to sequential —
  or mark a tool sequential — to serialize. A `beforeTool` gate and an
  `afterToolCall` hook bracket each call. Events are pushed to an `AgentSink`.
  Mirrors pi's event sequence (`agent_start → turn_start → message_* → tool_* →
  turn_end → agent_end`) plus its agent-owned steer / follow-up / next-turn
  message queues (`queue_update` events).
- **Full hook parity with pi's `AgentLoopConfig`, in one struct.**
  `AgentHarnessHooks` brackets the loop with typed C callbacks: `BeforeRun`,
  `BeforeContext`, `ConvertToLlm`, `BeforeProvider`, `AfterProvider`,
  `RewriteToolArguments`, `BeforeTool` (pi's `beforeToolCall` gate),
  `AfterTool` (pi's `afterToolCall`), `PrepareNextTurn`, `ShouldStopAfterTurn`,
  `GetSteeringMessages`, `GetFollowUpMessages`, `BeforeCompaction`,
  `BeforeNavigation`, `AfterTurn`. `PrepareNextTurn` swaps
  the model, effort, or context for the rest of a run and `ShouldStopAfterTurn`
  ends it gracefully; both fire after `turn_end` and before either steering
  source is polled. `GetSteeringMessages` / `GetFollowUpMessages` are polled at
  the same checkpoints as the in-memory queues. Overrides are run-scoped and
  never touch the config or the session tree; a failing hook surfaces as a
  typed `HookError`. `AgentHookChain` composes independent hook sets into one
  (the first blocking gate wins, rewrites pipe through in order, `AfterTurn`
  reaches everyone), so features never compete for a slot. The CLI builds on it
  with app extensions: the hooks.json policy, the memory wiki, and the shell
  tool's artifact directory each own their hooks and hear session changes,
  ordered events, and exit through one registry.
- **One reimplemented chat wire protocol**, not vendor SDKs. Chat uses
  `POST /v1/responses`; every chat provider is a row of
  endpoint data rather than a protocol implementation. The request builder and
  SSE parser are tested against recorded fixtures offline. See
  [Providers](CONFIGURATION.md#providers). Auto permission reviews reuse the
  same protocol and credentials, calling a cheap model on the active provider.
- **External lifecycle hooks** let an editor or supervisor follow a run without
  linking against Clippi. See [Lifecycle hooks](HOOKS.md).

## Scope

This is the small clean core.

Deliberately out of scope for now (the structure leaves seams for them):

- Other vendors' native protocols (Anthropic Messages, Google Gemini) and the
  subscription OAuth flows that depend on them. ChatGPT sign-in is supported
  because its backend speaks Responses.
- A bidirectional RPC mode. `-p` with `--mode text|json` is the non-interactive
  surface; there is no stdin-driven protocol yet.
- TUI niceties: visual-row (vs logical-line) Up/Down, syntax highlighting,
  images, undo. The full-screen differential TUI itself is done.
- Skills packages (`pi install`-style distribution), general extensions, and
  additional providers. Local skill and prompt-template discovery is done.
- pi's two-phase split-turn summary for a single turn that exceeds the
  retained-tail budget. One-shot `/compact <instructions>` is done.
- Fuzzy/regex session search, generated names, pricing, deletion UX, and a
  derived session metadata sidecar.
- Automatic dreaming. A pass runs when asked (`/dream`, `--dream`); nothing
  schedules one when a session ends.
