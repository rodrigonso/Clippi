# Usage

## Command line

```sh
clippi -p "hi"                # set OPENROUTER_API_KEY
clippi                        # full-screen TUI; /model picks a live model
clippi -m z-ai/glm-4.6 -p "summarize README.md"
clippi --permission-mode ask -p "review the working tree"
clippi --mode json -p "summarize README.md"   # NDJSON event stream
clippi --continue             # most recent session for this project
clippi --resume SESSION_ID     # exact ID, including sessions from other projects
clippi --no-session            # no session filesystem writes
clippi --system-prompt SYSTEM.md -p "review this project"
clippi --append-system-prompt "Prefer minimal changes"
clippi --no-context-files       # skip AGENTS.md / CLAUDE.md discovery
clippi --no-skills              # skip SKILL.md discovery
clippi --skill ./my-skill       # load one skill explicitly (repeatable)
clippi --no-prompt-templates    # skip prompt-template discovery
clippi --prompt-template ./review.md   # load one template (repeatable)
clippi -t read,ls,grep          # allowlist of tools to enable
clippi -xt shell                # disable specific tools
clippi --no-tools               # read-only conversation
clippi --max-turns 25           # ask to continue every 25 turns (0: unlimited)
clippi --approve                # trust project prompt files for this run
clippi --no-memory              # no memory wiki for this run
clippi --dream                  # consolidate this repository's memory wiki
```

## Tools

All eight tools (`read`, `write`, `edit`, `ls`, `find`, `grep`, `shell`,
`ask_user`) are enabled by default. The active set shapes the system prompt, so
disabling a tool also removes its snippet and guideline bullets.

```sh
clippi -t read,ls,grep      # allowlist: only these three
clippi -xt shell,write      # everything except these
clippi -nt                  # no tools at all
```

`--tools` (`-t`) and `--exclude-tools` (`-xt`) each take one comma-separated
list and may be repeated; exclusions are applied after the allowlist.
`--no-tools` (`-nt`) wins over both. Unknown tool names are an error rather
than being ignored, since without an extension mechanism an unrecognized name
is always a typo.

### Read and shell output limits

Text results keep at most 2,000 lines and 50 KiB: `read` returns the beginning
of the selected range, and `shell` returns the tail. Both use bounded working
buffers instead of retaining the entire file or command output in memory.
`read` still scans to EOF to report exact line counts and continuation offsets,
checking cancellation between chunks.

Once shell output exceeds either limit, Clippi creates its overflow log and
streams the full output to it, including the already captured prefix. The log
is left in the session's artifact directory (`shell-<hex>.log`), or the OS temp
directory under `--no-session`, and its path appears in the result. Small
commands create no log. Live previews reuse the bounded tail. An artifact
creation, write, or close failure is a tool error; any retained partial log is
reported as incomplete, never as full output. Disk use remains proportional to
output, and shell commands still have no default timeout.

Images are not text-truncated. `read` accepts at most **10 MiB of raw image
data**, then encodes it as base64 (about 13.34 MiB at the limit). Larger images
produce an explicit error. This is Clippi's resource limit, not a claim about
the provider's image limits.

### Search

The `grep` tool searches in-process and requires no external search executable.
Normal use does not download or install helper tools, libraries, or runtimes.
Regex patterns use PCRE2 syntax: common existing search options are preserved,
but this is not a byte-for-byte clone of ripgrep's Rust regex dialect.
Regex work is bounded; exceeding a matching limit returns an explicit tool error.
Explicit regex callouts and `\C` are rejected.

Files are streamed through one reusable line buffer. UTF-8 and BOM-marked UTF-16
are supported; NUL-containing binary files are skipped. Individual decoded lines
over 8 MiB fail explicitly rather than exhausting arena memory. Output defaults
to 100 matches and is capped at 50 KiB, with displayed lines truncated to 500
characters. Directory traversal does not recursively follow symlinks.
Search paths and filenames use UTF-8, including Windows Unicode descendants.
Negative directory globs prune their subtrees; brace alternatives and character
classes are supported, but nested brace alternatives are rejected explicitly.
Ignore handling reads `.gitignore` and `.ignore`, including parent scopes; Git's
global excludes and `.git/info/exclude` are not currently loaded.

### Asking the user

The `ask_user` tool lets the agent put a question to you mid-run and wait for
the answer, either free-form or as a fixed set of choices. It runs on its own,
never alongside another tool, so two questions can never compete for the
screen. With no user present it returns an error the model can read and work
around rather than blocking, which is what keeps `-p` and `--mode json` usable.

It is the same seam the turn limit uses, so a question looks identical
whichever raised it. In the TUI the modal is labelled `Question`, not
`Commands` — the breadcrumb reflects what opened the modal, so command-invoked
pickers like `/model` still read `Commands`.

## Interactive TUI

Running interactively on a real terminal launches a full-screen TUI: an
alternate-screen, double-buffered **differential renderer** (own cell grid +
scrollback), markdown-rendered assistant output (headings, lists, tables,
blockquotes, code blocks), a **sticky working-directory + git-branch line**
just above the prompt, a minimal status line along the bottom
(spinner and state on the left; model, supported effort, a small context-window
usage bar, and token usage right-aligned), and a **multi-line input editor**.
The bar appears only for models that publish a context window. It turns amber
past 70% of the window and red past 90%, and reads empty right after a
compaction until the next response reports usage. The first frame is
painted synchronously before
HTTP initialization, cwd/git context discovery, and credential warmup; key input
starts before the remaining post-frame work. It runs three threads — the agent
loop, a key-input thread, and a render thread (~20fps) so the spinner stays live
while a turn waits on the network. Assistant deltas are displayed at complete
UTF-8 boundaries on the next render frame, without a separate typing-speed
throttle, so reveal speed is bounded only by provider delivery and rendering.

The interface is assembled with the internal, arena-backed `clippi_ui`
component library. Layout, focus, hit-testing, pointer capture, reusable
widgets, and custom component integration are documented in
[`UI.md`](UI.md).

Keys: **Enter** sends · **Shift+Enter** / Ctrl+J / Alt+Enter inserts a newline ·
arrows move
(↑/↓ across lines, then through history at the edges) · **Ctrl+←/→** word jump ·
Home/End · **Shift** + movement selects composer text ·
**Ctrl+W/U/K** delete word / to start / to end · **mouse wheel** scrolls the
transcript · **left-drag** selects transcript or composer text (and auto-scrolls
the transcript) · an overflow-only scrollbar can be clicked or dragged (track
clicks center its thumb) · type during a turn to **steer** · **Ctrl+C** copies
selections via OSC 52 and confirms with a transient `copied N chars` message on
the status row; otherwise it interrupts a turn, cancels a command
operation, clears the composer, or quits · double-press **Escape** within 500 ms
to clear an idle composer or interrupt an active turn · hover or **left click**
a turn's first row to highlight or collapse/expand it · **Ctrl+O** expands all
tools when any is collapsed, otherwise collapses all · **Ctrl+D** quits ·
**Ctrl+L** repaints.
A `Thinking…` row with elapsed seconds marks model waits in the transcript, and
each run ends with a footer rule showing its duration, tool count, and token
totals. Notices render as italic asides behind a dim grey dot. The user
marker, spinners, and blockquote bars take the OS accent colour.
Honors `NO_COLOR`. Bracketed paste inserts literal multi-line text and replaces
a composer selection.

Until the first prompt, a new session shows the Clippi mascot, which blinks and
raises its brows now and then, with a short hint in the transcript area, below
any startup notices, when the terminal has room.
Shift+Enter needs a terminal that reports modified keys: entering the TUI
requests the kitty keyboard protocol (kitty, WezTerm, Ghostty, foot, Alacritty,
iTerm2, Windows Terminal 1.25+) and xterm's `modifyOtherKeys` level 2 (xterm,
VTE/GNOME Terminal, Konsole), and releases both on exit. Terminals that
implement neither — including legacy Windows conhost — cannot distinguish
Shift+Enter from Enter, so it submits there; Ctrl+J and Alt+Enter work
everywhere.

When stdin/stdout isn't a TTY (pipes, redirected) or with `-p`, it falls back to
a plain line renderer. Plain and piped output write provider deltas immediately
without display pacing. Session commands remain available; interactive
selectors such as `/auth` and `/model` require a TTY.

Interactive commands:

- `/auth` — choose OpenRouter or ChatGPT, sign in in the browser, then choose a
  model.
- `/model` — choose a live-discovered or manual model ID for the current
  provider, then choose
  the effort and context tier when that model exposes those controls. Fixed
  context windows are displayed without an unnecessary prompt.
- `/session` — inspect the current title, IDs, project, status, times, model,
  counts, token categories, and fork ancestry.
- `/name [title]` — set the current session name; an empty TUI value or
  `/name --clear` restores the first-user-message title.
- `/sessions [all]` — browse rich current-project metadata, or all projects.
- `/resume [id]` — choose an available session, including an all-project page,
  or switch directly by exact full ID.
- `/tree` and `/checkout <entry-id>` — inspect or navigate conversation branches.
- `/fork [entry-id]` — fork and switch to a new session.
- `/compact` — durably summarize the active branch (compaction also runs
  automatically; see [Compaction](CONFIGURATION.md#compaction)).
- `/trust` — save project trust for a future Clippi process.
- `/permissions [ask|auto|yolo|reset]` — inspect or change [tool permissions](PERMISSIONS.md);
  opens a settings menu in the TUI; `reset` clears session grants and restores Ask.
- `/permissions key [clear]` — configure the Jev API key through masked entry
  or remove the saved key; never accepts a key as a command argument.
- `/permissions model [jev-...]` — inspect or save and apply the Jev reviewer
  model while idle; defaults to `jev-latest`.
- `/permissions threshold [0-1]` — inspect or save and apply the Auto review
  threshold while idle; applies to both judgments and defaults to `0.95`.
- `/reload` — transactionally reload settings and prompt context while idle.
- `/debug [on|off]` — toggle the [performance overlay](#debug-overlay). TUI only.
- `/rounded [on|off]` — toggle [rounded corners and bullets](#rounded-style).
  TUI only; the choice is saved to `settings.json`.
- `/memory`, `/dream` — inspect and consolidate the [memory wiki](MEMORY.md).
- `/skill:<name>` and prompt templates — see [Skills](SKILLS.md).
- `/clear` — create a fresh session while preserving the provider, model,
  effort, and context selection.
- `/exit` (or `/quit`) — close Clippi and return to the shell.

Typing `/` opens a command flyout above the composer that matches command
names, aliases, and descriptions as case-insensitive substrings. **Up/Down**
selects, **Tab** completes the name without running it, **Enter** runs it, and
**Escape** dismisses it. In selection pages, typing filters the displayed
options and Backspace on an empty filter goes back.

Commands entered during a TUI turn are refused and stay in the composer; press
Escape twice to interrupt, then send again. `/debug`, `/exit`, and `/clear` act
immediately. Ordinary text steers the active turn and appears as a
`↳ steer` turn.

The selected provider/model, explicit effort/context preferences,
`defaultProjectTrust` policy, permission mode and reviewer model, and the
`rounded` UI style are written atomically to `~/.clippi/settings.json`.
New sessions use built-in defaults, then settings, then the runtime-only `-m`
override. Resumed sessions restore their durable provider/model ancestry; `-m`
adds a new durable model selection. Direct IDs are exact opaque durable IDs.
Cross-project resume enters the session's stored working directory, resolves
project trust again, rebuilds prompt context, and rollback-safely switches the
session, process cwd, and TUI cwd/branch. `--continue` remains scoped to the current project. Sessions
live under `~/.clippi/sessions/v2/<project-key>/`. Canonical JSONL files also store names without
a sidecar. A writer holds a `<session-id>.lock` sentinel beside its log for as
long as it is open; the sentinel is removed on release, and on Windows the
kernel removes it even if the process is killed. A `<session-id>/` directory
beside the log holds that session's other artifacts, currently the `shell`
tool's output-overflow logs; it is created only when something needs it.
Unreadable session files are reported and skipped during
browsing/continuation. Interactive
mode may start without credentials and blocks prompts with `/auth` guidance;
`-p` remains fail-fast.

Set `CLIPPI_PRINT_MEM=1` to print peak RSS at exit.

### Debug overlay

`/debug` toggles a performance panel in the top-right corner of the TUI;
`/debug on` and `/debug off` set it explicitly. It reports the frame rate, the
mean and worst frame interval over the last 64 frames, and — for the most recent
turn — time to first token, wall time, token usage, end-to-end output tokens per
second, and the last tool's duration. It also reports current process resident
memory (RSS).

The panel only paints. It never takes focus or receives input, and modals and
the command flyout draw over it. Timing samples accumulate while the panel is
hidden, so the window is already full when it is switched on. RSS is queried
when the panel appears and at most once per second while it remains visible.
The state is per-run and always starts off — nothing about it is written to
`settings.json`.

### Rounded style

`/rounded on` uses `╭ ╮ ╰ ╯` borders and `●`/`○` transcript bullets.
`/rounded off` uses `┌ ┐ └ ┘` and `◼`/`◻`. The choice is stored as `"rounded"`
in `~/.clippi/settings.json`; `/reload` applies edits made to the file.

## JSON mode

`--mode json` turns print mode into a machine surface: one JSON object per line
on stdout, flushed as it happens. All human-facing output stays on stderr, so
stdout is safe to pipe.

Machine events are observed synchronously before the presentation watch queue:
they are never coalesced, dropped, or reconstructed after a lag. A slow pipe
applies backpressure to the producer rather than growing an event backlog.

```sh
clippi --mode json -p "summarize README.md"
```

```json
{"type":"agent_start","turn":"run"}
{"type":"record","kind":"message","record":{...}}
{"type":"turn_start","turn":"run"}
{"type":"text_delta","index":0,"text":"pong"}
{"type":"turn_end","turn":"run","outcome":"succeeded"}
{"type":"agent_end","turn":"run","outcome":"succeeded"}
{"type":"result","ok":true}
```

Event types: `agent_start`, `agent_end`, `turn_start`, `turn_end`,
`text_delta`, `thinking_delta`, `tool_call_delta`, `tool_start`, `tool_update`,
`tool_end`, `retry_start`, `retry_end`, `queue_update`, `cancel_requested`,
`record`, and a final `result`.

`record` carries a durable session entry verbatim, in the same schema the
session log stores, so a consumer sees exactly the state that was persisted
rather than a re-flattened copy. `agent_end` always arrives before `result`,
and `result.ok` matches the process exit code. A failed run adds
`result.error`; a run that succeeded but stopped early — today only at the turn
limit — adds `result.message` instead.

The terminator is the last stdout line, including when a successful run carries
a notice. Encoding, write, or flush failures cancel production and cause a
nonzero exit with a stderr diagnostic; an unusable output stream cannot be
promised a final `result`. Startup failures also report on stderr before a run
and its event stream begin.

`--mode json` requires `-p`; there is no interactive form without a
bidirectional protocol.
