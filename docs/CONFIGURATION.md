# Configuration

## Project context and system prompts

Clippi builds the system prompt from the active tool catalog, project
instructions, and cwd. It loads the first regular file named
`AGENTS.override.md`, `AGENTS.md`, `AGENTS.MD`, `CLAUDE.md`, or `CLAUDE.MD`
from `~/.clippi` and each directory from the filesystem root through cwd.
`AGENTS.override.md` therefore wins over the other candidates in its own
directory, while every other directory still layers normally. Global context is
applied first, followed by broad-to-specific project context. Use
`--no-context-files` (`-nc`) to disable this discovery.

Tools contribute their own prompt text: a one-line snippet for the `Available
tools` list and optional guideline bullets that are merged, in tool order, with
the always-on guidelines. A tool without a snippet is not listed.

`~/.clippi/SYSTEM.md` replaces the built-in prompt globally, and
`~/.clippi/APPEND_SYSTEM.md` appends instructions. Trusted projects can override
those files with `<cwd>/.clippi/SYSTEM.md` and
`<cwd>/.clippi/APPEND_SYSTEM.md`. `--system-prompt` and repeatable
`--append-system-prompt` values have highest precedence; each value is read as
a file when it names an existing regular file, otherwise it is treated as
literal text. Custom prompts still receive project context and cwd.

Project prompt overrides require trust. Decisions are stored hierarchically in
`~/.clippi/trust.json`; the nearest saved ancestor wins. Set
`defaultProjectTrust` in `~/.clippi/settings.json` to `ask` (default), `always`,
or `never`. `--approve` (`-a`) and `--no-approve` (`-na`) override trust for one
run. `/trust` saves a decision for the current or parent folder and requires a
restart; `/reload` reloads settings and prompt/context files while preserving
the current process's resolved trust. If gated project prompt files first
appear after startup, `/reload` fails closed and requires a restart unless an
explicit CLI trust override covers the run.

`~/.clippi/settings.json` also stores the active provider/model and explicit
effort or selectable context-window preferences. Unsupported saved values fall
back to the selected model's defaults. Current fixed provider context windows are displayed without an extra chooser.

## Providers

Clippi supports two chat providers. Both speak the Responses API shape
(`POST <base>/responses`, SSE `response.*` events) through one client:
OpenRouter translates that protocol for the upstream model vendor, and ChatGPT
is OpenAI's Codex backend, reached with a ChatGPT Plus/Pro sign-in.

| Provider | Default base URL | API key env | Base URL env |
| --- | --- | --- | --- |
| `openrouter` | `https://openrouter.ai/api/v1` | `OPENROUTER_API_KEY` | `OPENROUTER_BASE_URL` |
| `openai-codex` | `https://chatgpt.com/backend-api` | — (sign-in only) | `OPENAI_CODEX_BASE_URL` |

`openrouter/auto` is the bootstrap default. It is OpenRouter's stable automatic
router, not a compiled model-availability table. `gpt-6.1-sol` is the ChatGPT
provider's default until a model is chosen.

```sh
OPENROUTER_API_KEY=... \
  clippi -m anthropic/claude-sonnet-4.5 -p "summarize README.md"
```

`/model` fetches the provider's live catalog: OpenRouter's
`GET /api/v1/models`, or the Codex backend's `GET /codex/models`. OpenRouter
models enter the picker only when the catalog advertises tool use; advertised
modalities must also include text input and text output. ChatGPT models are
those the backend lists as visible, in its order. Context windows, reasoning
support, and reasoning-effort tiers come from that response. Clippi has no
compiled model list and never infers capabilities from model IDs. If discovery
is unavailable, manual model-ID entry remains available.

Each successful listing is cached in `~/.clippi/models/<provider>.json`.
Startup, resumed and switched sessions, and dreams read it, so the
saved effort, the context bar, and auto-compaction apply without a network
round trip. A model the cache has never seen starts without that metadata until
`/model` lists it.

The ChatGPT dialect differs from OpenRouter's only where the backend requires
it: requests carry the `chatgpt-account-id` from the sign-in, always include
instructions, send no output-token limit, and pass effort names through
unchanged. Usage counts against the ChatGPT plan's limits rather than API
billing. Whether a given plan permits use from a third-party client is
governed by OpenAI's terms; check them before relying on it.

Why Responses and not Chat Completions: the models Clippi targets reject
`reasoning_effort` alongside function tools on `/v1/chat/completions`, and Chat
Completions standardizes no way to return thinking to the server. Responses
accepts both, and `include: ["reasoning.encrypted_content"]` with `store: false`
lets Clippi keep owning the transcript while still replaying reasoning across
turns.

Deliberate limits:

- **ChatGPT is the only subscription sign-in.** Claude Pro/Max tokens are
  issued for a protocol Clippi does not speak. Auto permission reviews use a
  separate TypeSafe runtime environment key.
- **Reasoning replay is model-scoped.** Encrypted reasoning and item IDs are
  only valid for the model that produced them, so a mid-session model switch
  drops prior reasoning and regenerates IDs rather than replaying stale ones.
- **No other direct chat vendor transports.** Beyond ChatGPT, model reach and
  routing are whatever the current OpenRouter catalog and account expose.

## Authentication

Chat credentials are stored in `~/.clippi/auth.json` (atomic writes, restricted
perms). The resolved token is cached in the running app; storing a key, provider
changes, and `/reload` invalidate or replace the cache.

- **Sign in to OpenRouter** — run `/auth` in the TUI and choose OpenRouter.
  Clippi opens your browser to
  OpenRouter, where you approve it; OpenRouter then mints an API key labeled
  `Clippi` on your account (OAuth PKCE), which Clippi stores. The redirect is
  received by a one-shot listener on `127.0.0.1`. Under SSH, or when no
  browser can be opened, Clippi prints the sign-in URL instead and you paste
  the code OpenRouter shows. Revoke the key from your OpenRouter key settings.
- **Sign in to ChatGPT** — run `/auth` and choose ChatGPT. Clippi opens
  `auth.openai.com` (OAuth PKCE, the same client the Codex CLI uses), and the
  redirect returns to `http://localhost:1455/auth/callback`. That port is fixed
  by the client registration; if it is busy (for example, the Codex CLI is
  signing in) or no browser can be opened, paste the address you were
  redirected to. Clippi stores an `oauth` entry — access token, refresh token,
  expiry, and ChatGPT account id — and refreshes the access token
  automatically shortly before it expires. Refreshes are serialized across
  Clippi processes with a lock beside `auth.json`, since each refresh token
  can be used only once. There is no environment-variable form.
- **Environment** — set `OPENROUTER_API_KEY` for `-p`, CI, and other headless
  runs. A credential is always required.

Resolution priority: stored api_key → env var for OpenRouter; the stored
sign-in for ChatGPT. `--logout <provider>` removes chat credentials. The Auto
permission reviewer's TypeSafe key is configured separately; see
[Permissions](PERMISSIONS.md).

## Compaction

Clippi compacts automatically. Before each provider request the agent loop
compares the branch's context against the active model's window; once the
context has eaten into the reserved headroom it folds the older part of the
branch into a single summary and continues. The most recent messages are kept
verbatim, and the retained tail always begins at a user turn, so an assistant
tool call is never separated from its tool results.

Both repositories validate compaction topology before accepting any part of an
append batch: the source head must equal the parent, and the summarized endpoints
must be ordered ancestors on that branch. Earlier entries in the same batch are
included. Invalid batches leave storage unchanged; loading an existing malformed
log reports corruption rather than accepting a session that fails during replay.

Token pressure is read from the provider's reported usage when there is any
(prompt + cache + output of the newest assistant turn, plus an estimate for
anything appended since), and falls back to a byte-based estimate otherwise.

Every summary ends with the files the folded turns touched, taken from the
`path` argument of `read`, `write`, and `edit` tool calls:

```
<read-files>
docs/design.md
</read-files>

<modified-files>
src/agent/driver.cpp
</modified-files>
```

A path that was written or edited is reported only as modified. The lists are
stored on the compaction record as well as in the summary text, and each
compaction reseeds from the ones before it, so paths survive any number of
successive compactions even though the turns they came from are gone. Files
created by a `shell` command are not tracked, since only those three tools
publish a path argument.

Compaction runs at most once per turn; if it succeeds and the context is still
over budget, the request proceeds and the provider decides. If the compaction
turn itself fails, the run stops with an `automatic compaction failed` error
rather than letting an oversized request through.

Tune or disable it in `~/.clippi/settings.json`:

```json
{
  "compaction": {
    "enabled": true,
    "reserveTokens": 16384,
    "keepRecentTokens": 20000
  }
}
```

`/compact` remains available and always folds the whole branch. It accepts
optional one-shot instructions — `/compact keep only the API decisions` — which
are appended to the summarization prompt for that compaction only. They are
never persisted and never applied to automatic compaction.

### Idle compaction

Providers evict their prompt cache after a few minutes without requests, and
the next request then pays for the whole prefix anyway. So when the TUI has
been idle that long after a prompt, Clippi compacts while you are away: it
folds everything beyond `keepRecentTokens`, exactly as automatic compaction
would, and leaves smaller sessions alone. With the memory wiki on it also
starts a background dream (see [MEMORY.md](MEMORY.md)). This is a deliberate
divergence from pi, which compacts only near the context limit.

It happens at most once per idle stretch, only in the TUI, never while a draft
is in the composer or a dialog is open, and never for a session you switched
away from. Interrupt it like any turn. The wait comes from the provider (10
minutes for OpenRouter and ChatGPT, the upper end of OpenAI's documented
eviction window); override it, or turn the feature off with `0`:

```json
{
  "cacheIdleMinutes": 10
}
```

The value is whole minutes from 0 to 1440. `compaction.enabled: false` turns
off idle compaction too, but not idle dreaming.

## Retries

Provider requests that fail with a retryable error are retried by the agent
loop. Retries are on by default: three attempts, 2 s base delay, doubling per
attempt (2 s, 4 s, 8 s) with no jitter, capped at 8 s. Tune or disable in
`~/.clippi/settings.json`:

```json
{
  "retry": {
    "enabled": true,
    "maxRetries": 3,
    "baseDelayMs": 2000,
    "maxDelayMs": 8000
  }
}
```

Setting `maxRetries` to 0 or `enabled` to `false` fails on the first error. The
HTTP transport underneath does not retry independently, so these numbers are
the whole story.

## Turn limit

The agent loop keeps taking turns while the model keeps calling tools. A model
that never stops would otherwise run until the context fills or you cancel it,
so the loop carries a budget: after 100 consecutive turns with no input from
you, it stops and asks whether to keep going.

Answering *Continue* grants another full budget. Answering *Stop* — or
dismissing the question — ends the run cleanly with a notice saying how many
turns it took; the branch is intact and an ordinary follow-up prompt resumes
from exactly where it stopped. Steering or a follow-up message you send mid-run
also resets the budget, since the loop is no longer running unattended.

```json
{
  "turnLimit": {
    "enabled": true,
    "maxTurns": 100
  }
}
```

```sh
clippi --max-turns 25      # ask every 25 turns
clippi --max-turns 0       # never ask; the loop is unbounded again
```

When nobody can answer — under `-p`, `--mode json`, or a non-interactive
terminal — the run stops at the limit rather than blocking. Set `maxTurns` to
`0` or `enabled` to `false` for the old unbounded behavior.
