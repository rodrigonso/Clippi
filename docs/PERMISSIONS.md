# Permissions

Clippi includes a built-in permission policy for tool calls. Select one of three
modes with `--permission-mode` for a single run, or `/permissions` inside an
interactive session:

- **Ask** runs inspection tools directly and prompts before writes, edits,
  shell commands, and unknown tools.
- **Auto** also admits workspace-local writes/edits and a narrow reversible
  shell allowlist. Writes under a `.git`, `.clippi`, or `.agents` directory
  do not count as workspace-local, since they steer later execution. Other
  actions with complete review context go to a fast, cheap model on the active
  chat provider (the **reviewer**); an action too long to show in full skips
  the reviewer and waits for a person. A non-allow becomes a failed tool result
  carrying an opaque request ID; the model must pass that exact ID to
  `ask_user` to open the real approval prompt.
- **Yolo** bypasses the built-in checks and reviewer, but not external
  `PreToolUse` hooks. An unacknowledged Yolo activation warns prominently.

`Always for this session` approvals are held in memory and last only for the
current harness session. `/permissions reset` clears those grants and restores
Ask. New installations default to Auto.
In the TUI, `/permissions` opens a settings menu for the permission mode,
reviewer model, and session grants. Outside the TUI it reports the active
mode, reviewer model, and grant count. In the TUI, **Shift+Tab** cycles
`ask -> auto -> yolo -> ask`; the footer shows the active mode. JSON print mode
emits `permission_review_start` and `permission_review_end` around review
calls; the end event reports the reviewer model.

## The reviewer

Each review is one request to the active provider, using the same Responses
protocol and the same sign-in or API key as chat, at the model's lowest
reasoning effort. The reviewer sees only the tool name, the exact action, and
the user's own recent words -- never the transcript or tool output -- encoded
as JSON data it is told never to take instructions from. It must answer by
calling a strict `permission_decision` function with `allow` or `ask` and a
one-sentence reason. It allows only actions the user's request asks for or
clearly implies **and** whose consequences are routine: confined to the
project, reversible, and free of data loss, secret access, privilege changes,
persistence, publishing or pushing, payments, messages to other people, and
network transfers beyond fetching declared dependencies.

Anything else fails closed to a human: an `ask` answer, prose instead of the
function call, malformed or unknown values, a request error, or cancellation.
The provider's transport retries apply. This is a model judgment, not a
calibrated safety guarantee; **no evaluation of reviewer accuracy has been
performed**.

The reviewer model belongs to the active provider and follows provider
switches. Defaults are `openai/gpt-6-luna` on OpenRouter and `gpt-6-luna` on
ChatGPT. Model IDs differ between providers, so overrides are kept per provider
in `~/.clippi/settings.json`:

```json
{
  "permissionReviewerModels": {
    "openrouter": "anthropic/claude-haiku-5.5",
    "openai-codex": "gpt-6-luna"
  }
}
```

In-app, `/permissions model <model id>` saves and applies an override for the
active provider while the agent is idle; `/permissions model default` removes
it, and `/permissions model` shows the active model and where it came from.
Invalid input or a save failure leaves the active reviewer unchanged. The
retired single-string `permissionReviewerModel` (which only ever named TypeSafe
`jev-*` models) and `permissionReviewerThreshold` keys are ignored and dropped
on the next save; `jev-*` values inside `permissionReviewerModels` are ignored
too.

Missing or incomplete action projections, actions over 2,048 bytes, truncated
trusted user text, or non-text user content require human approval without a
review request. Filesystem projections omit file contents and edit payloads and
are marked incomplete: workspace-local write/edit fastpaths remain unchanged,
but outside-workspace writes/edits require human approval rather than a
misleading path-only review. Shell actions preserve their raw newlines for the
reviewer; only the human-facing display is sanitized. Cached review decisions
are revalidated when user text/context, reviewer configuration, or the
permission-mode revision changes; an explicit Deny remains blocked.

These controls are separate from project trust and `PreToolUse` hooks. Project
trust decides whether prompt-resource files are loaded; tool permissions decide
whether a tool call is admitted; `PreToolUse` is an independent external gate
that can still block a call. Clippi provides no sandbox: approved tools run
with the process user's filesystem and OS privileges. In headless mode there is
no human approval path, so Ask and an Auto action the reviewer did not allow
fail closed. This feature does not attempt filesystem sandboxing, persistent
built-in rule files, or network isolation.
