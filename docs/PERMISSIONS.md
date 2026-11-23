# Permissions

Clippi includes a built-in permission policy for tool calls. Select one of three
modes with `--permission-mode` for a single run, or `/permissions` inside an
interactive session:

- **Ask** runs inspection tools directly and prompts before writes, edits,
  shell commands, and unknown tools.
- **Auto** also admits workspace-local writes/edits and a narrow reversible
  shell allowlist. Writes under a `.git`, `.clippi`, or `.agents` directory
  do not count as workspace-local, since they steer later execution. Other
  actions with complete review context go directly to TypeSafe Jev over HTTPS,
  independently of the chat provider and model. This is a separate service
  request; an action too long to show in full skips Jev and waits for a
  person. A non-allow becomes a failed tool result carrying an opaque request
  ID; the model must pass that exact ID to `ask_user` to open the real
  approval prompt.
- **Yolo** bypasses the built-in checks and reviewer, but not external
  `PreToolUse` hooks. An unacknowledged Yolo activation warns prominently.

`Always for this session` approvals are held in memory and last only for the
current harness session. `/permissions reset` clears those grants and restores
Ask. New installations default to Auto with reviewer `jev-latest`.
The global `permissionMode`, `permissionReviewerModel`, and
`permissionReviewerThreshold` are configurable in `~/.clippi/settings.json`.
In the TUI, `/permissions` opens a settings menu for the permission mode, Jev
key, model, threshold, and session grants. Outside the TUI it reports the active
mode, reviewer, threshold, and grant count.
`/permissions model` shows the active Jev model. To pin a version in-app, use
`/permissions model jev-1.13.0`; `/permissions model jev-latest` restores the default.
This saves and applies the model immediately while the agent is idle, without
changing the chat model, threshold, or session grants. Invalid input or a save
failure leaves the active reviewer unchanged. The default `jev-latest` follows
stable Jev releases; `jev-latest` and `jev-preview` can move between versions.
In the TUI, **Shift+Tab** cycles
`ask -> auto -> yolo -> ask`; the footer shows the active mode. JSON print mode
emits `permission_review_start` and `permission_review_end` around
review calls; the end event reports the returned reviewer model when available.

Jev's automatic approval policy requires **routine** consequences and an
**authorized** action (`within_request`), each label with probability at least
`permissionReviewerThreshold` (**0.95** by default). Set this to a number from
**0 to 1**, inclusive; higher values require more certainty. For example, add
`"permissionReviewerThreshold": 0.9` to settings and run `/reload` to apply it.
Alternatively, `/permissions threshold 0.9` saves and applies the value
immediately while the agent is idle, without `/reload`. `/permissions threshold`
shows the current value without changing settings. Invalid input or a save
failure leaves the active threshold unchanged.
Reloading invalidates cached judgments without clearing explicit session grants.
The threshold applies separately to both selected labels, not their average or
TypeSafe's distribution-derived `confidence` field. Even at zero, other labels
and invalid responses cannot auto-approve. Invalid thresholds are rejected.
This is a provisional application threshold, not a calibrated safety
probability or evaluated model guarantee. **No live classifier evaluation has
been performed.** Other labels, low
probabilities, malformed responses, and service failures do not auto-approve;
the user can still approve through the existing request-ID flow.

Missing or incomplete action projections, actions over 2,048 bytes, truncated
trusted user text, or non-text user content require human approval without a
Jev request. Filesystem projections omit file contents and edit payloads and
are marked incomplete: workspace-local write/edit fastpaths remain unchanged,
but outside-workspace writes/edits require human approval rather than a
misleading path-only review. Shell actions preserve their raw newlines for the
reviewer; only the human-facing display is sanitized. Cached review decisions
are revalidated when user text/context, reviewer configuration, or the
permission-mode revision changes;
an explicit Deny remains blocked.

Automatic classification of unresolved Auto actions requires the TypeSafe
service. There is **no old-model fallback** or local reviewer operating mode.
Use `/permissions key` to paste a Jev API key into a dedicated masked dialog.
The key is saved separately from settings in `~/.clippi/auth.json`, under
`typesafe`. The reviewer resolves its credential at review time; the
saved key takes precedence over `TYPESAFE_API_KEY`. `/permissions key clear`
removes the saved key and restores environment-variable fallback. Saving or
clearing a key invalidates cached permission judgments without clearing grants.

Masked key fields are never added to composer history, transcript/session
messages, clipboard output, or command notices, and temporary input buffers are
wiped after use. Keys must not be supplied inline in commands. Authentication
and permission command arguments are omitted from history and command
notices. Plain terminal entry disables echo and fails rather than accepting an
unprotected secret.
Oversized interactive lines are rejected and drained, never split into another
command or prompt.
This protects the UI/transcript path; `auth.json` remains the existing local
credential file, not an encrypted vault.

Responses are capped at **32 KiB**. Each attempt has a
**30-second timeout**, with **at most two retries** honoring the existing server
retry hints. Changing or restoring a chat provider/model never changes the Jev
reviewer.

The `permissionReviewerModel` setting accepts supported Jev model names and
aliases. An absent key selects `jev-latest`. Empty, non-string, or
unsupported values are configuration errors, including non-Jev model IDs.
Invalid reviewer settings prevent startup; `/reload` rejects them without
changing the active configuration. Ask, Yolo, deterministic fastpaths, session
grants, and external hooks are unchanged. Automated tests use deterministic
injected callbacks/transports; these tests are not an offline operating mode.

These controls are separate from project trust and `PreToolUse` hooks. Project
trust decides whether prompt-resource files are loaded; tool permissions decide
whether a tool call is admitted; `PreToolUse` is an independent external gate
that can still block a call. Clippi provides no sandbox: approved tools run
with the process user's filesystem and OS privileges. In headless mode there is
no human approval path, so Ask and an unavailable Auto reviewer fail closed.
This feature does not attempt filesystem sandboxing, persistent built-in rule
files, or network isolation.
