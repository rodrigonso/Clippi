# Memory wiki

Each repository gets a wiki of linked markdown pages that persists across
sessions: what the agent learned about the user, the project, and the codebase
that a future session would otherwise rediscover or get wrong. Every worktree
and subdirectory of one repository shares it -- the location follows the git
common directory, so linked worktrees resolve to the main checkout -- and a
folder outside git gets its own.

```text
~/.clippi/memory/<repo-key>/
  pages/<slug>.md   the wiki
  journal.jsonl     sessions and page writes, recorded by Clippi
  .dream/           dreaming state, progress, inbox, snapshots, and lock
```

A page is `<slug>.md` (lowercase-kebab-case) with a scalar header, and links
others with `[[slug]]`, so the folder also opens as an Obsidian vault:

```markdown
---
title: Build and test
type: feedback        # user | feedback | project | reference | topic
summary: Always build with build.bat; it is the page's catalog entry
pinned: false
---
The user asked for build.bat over raw CMake.
```

When `read` is available, the system prompt gains a `<memory_wiki>` block after
the project context: what the wiki is for, the full text of pinned pages (4 KB
in total), and a catalog of every page's summary (200 lines / 25 KB, pinned
first, then most recently changed). The catalog is generated from the page
headers, never stored, so it cannot drift from the pages or be clobbered by two
sessions at once. The agent reads pages with `read` and searches them with
`grep`; with `write` or `edit` it is also told when and how to write pages. The
wiki is read when the prompt is built (startup, `/reload`, a project switch),
so pages written mid-session reach the next session and the prompt stays
stable within one.

The pages folder is a writable root: a write or edit there is allowed exactly
like one in the working directory (Auto skips the reviewer; Ask still asks).
`write` and `edit` are refused for the journal and `.dream/`. Every page the agent writes
is checked as it is saved -- header fields, slug, size, and links to pages that
do not exist -- and problems come back in the tool result. With memory off the
system prompt is byte-for-byte what it is without the feature, which is the
only place it diverges from pi.

`memory` in `settings.json` (default `true`) turns the wiki on; `--no-memory` or
`CLIPPI_NO_MEMORY=1` turns it off for one run. `/memory` shows the wiki's
location, size, lint summary, and dreaming status; `/memory lint` lists
findings, `/memory on|off` switches it, and `/memory restore` puts back the
snapshot taken before the last dream.

## Dreaming

Dreaming is an offline pass in which an agent replays what recent sessions did
and corrects, extends, merges, and prunes the wiki. `/dream` starts it in the
background and `/dream watch` follows it live; `clippi --dream` runs it in the
foreground for the working directory's repository. A pass:

1. Takes `.dream/lock`, so only one pass runs per repository.
2. Reads the journal for sessions that worked in the repository -- from any
   worktree -- and renders the new part of each transcript into
   `.dream/inbox/` as a markdown digest (at most a third of the dream model's
   context window and 2 MB in total, so a backlog drains over several passes;
   user and assistant text, compact tool calls, failed tool results in full,
   other results clipped, compaction summaries, never reasoning).
3. Snapshots `pages/` (the newest three are kept).
4. Runs a headless agent with the wiki as its working directory, `read`,
   `write`, `edit`, `ls`, `find`, and `grep`, and writes confined to `pages/`.
   It may read the repository to check claims against the code.
5. Lints the result and spends up to two follow-up turns on remaining errors.
6. Advances `.dream/state.json` and journals the outcome. A failed pass
   advances nothing and is retried from the same point.

A pass with no new session activity, no page writes, and a clean wiki does not
start the agent. The dream's system prompt can be replaced by `~/.clippi/DREAM.md`,
or by `<repo>/.clippi/DREAM.md` in a trusted repository, using `{{inbox}}`,
`{{pages}}`, `{{repo}}`, `{{page_limit}}`, and `{{pinned_limit}}`.
`dreamModel` in `settings.json` picks a different (cheaper) model for dreaming;
`-m` overrides it. The dream's own session is durable and can be inspected with
`--resume` like any other.

`/dream watch` follows a dream as it happens, starting one first if none is
running. The transcript is replaced, read-only, by the dream's session -- its
first prompt, every tool call and result, and its closing report -- which grows
as the pass writes to its log; the status bar shows the phase. Ctrl+C, or
submitting anything, returns to the session as it was; leaving does not stop
the dream. The pass reports its phase, session, and outcome in
`.dream/progress.json`, which is what lets any process follow it.
