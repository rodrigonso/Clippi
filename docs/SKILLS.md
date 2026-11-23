# Skills and prompt templates

## Skills

Clippi implements the [Agent Skills](https://agentskills.io/specification)
standard. A skill is a `SKILL.md` file with YAML frontmatter carrying a `name`
and a required `description`; everything after the frontmatter is the body.

```markdown
---
name: release-notes
description: Draft release notes from the commits since the last tag.
---

Read `CHANGELOG.md`, then summarize each merged change in one line.
```

Only the name, description, and file location enter the system prompt, as an
`<available_skills>` block placed after the project context. The body is loaded
on demand — either the model calls `read` when a task matches the description,
or the user runs `/skill:<name>`. The block is emitted only when the `read`
tool is active, since nothing could load the bodies otherwise. A skill whose
frontmatter sets `disable-model-invocation: true` is hidden from the prompt and
reachable only through `/skill:`.

Skills are discovered from, in order:

1. `~/.clippi/skills/`
2. `~/.agents/skills/`
3. `.agents/skills/` in each directory from the repository root through cwd
4. `<cwd>/.clippi/skills/`
5. the `skills` array in `~/.clippi/settings.json`
6. `--skill <path>`, repeatable

Roots 3 and 4 are project-local and require trust, like project prompt
overrides. `--no-skills` disables roots 1–5; `--skill` still loads, so
`--no-skills --skill ./one` loads exactly one skill.

**The first root to claim a name wins**, so a user-scoped skill shadows a
project skill of the same name rather than the other way round. This is
deliberate parity with pi, and it is the opposite of the usual override
direction — rename the project skill if you want both.

Within a root, a directory containing `SKILL.md` is one skill and is not
descended into; its subdirectories are that skill's own resources. Otherwise
subdirectories are searched recursively, skipping dot-directories and
`node_modules` and honoring `.gitignore`, `.ignore`, and `.fdignore`. Bare
`*.md` files count as skills only at the root of a skills directory. Duplicate
paths reached through symlinks are loaded once.

`/skill:<name> [instructions]` submits the skill body as a prompt, wrapped so
relative references resolve against the skill's own directory, with any trailing
instructions appended. Loaded skills appear in the TUI command flyout, and
`/reload` re-runs discovery.

```sh
clippi --skill ./tools/review-skill      # load one skill explicitly
clippi --no-skills                        # skip all skill discovery
```

## Prompt templates

A prompt template is a `.md` file whose body becomes a slash command. Unlike
skills, templates never enter the system prompt — the model does not know they
exist. They are shorthand for the human at the keyboard.

```markdown
---
description: Review a file for correctness.
argument-hint: <path> [focus]
---

Review $1 for correctness. Focus on ${2:-edge cases and error paths}.
```

Saved as `review.md`, that becomes `/review src/agent/driver.cpp`.

Templates are discovered from, in order:

1. `~/.clippi/prompts/`
2. `<cwd>/.clippi/prompts/`
3. the `promptTemplates` array in `~/.clippi/settings.json`
4. `--prompt-template <path>`, repeatable

The name is the filename without `.md` — frontmatter cannot change it. Only
`description` and `argument-hint` are read, and neither is required; with no
description, the first body line (truncated to 60 characters) is used in the
command flyout. Directories are scanned non-recursively, and dotfiles are
skipped so editor lock files never load. As with skills, **the first root to
claim a name wins**, so a user-scoped template shadows a project one. Root 2 is
project-local and requires trust. `--no-prompt-templates` disables roots 1–3;
`--prompt-template` still loads. Built-in commands are matched first, so a
template cannot shadow `/compact`.

Arguments are split on whitespace, honoring `'` and `"` quoting (quotes are
stripped; there is no backslash escaping). Placeholders:

| Form | Expands to |
| --- | --- |
| `$1`, `$2`, … | that positional argument, or nothing if absent |
| `$ARGUMENTS`, `$@` | the full argument string |
| `${1:-text}` | `text` when argument 1 is missing or empty |
| `${ARGUMENTS:-text}` | `text` when no arguments were given |
| `${@:2}` | arguments 2 through the end, space-joined |
| `${@:2:3}` | three arguments starting at 2 |

`${1}` without a default is **not** a placeholder and is left as literal text,
matching pi. A template with no placeholders discards any arguments it is
given. Loaded templates appear in the TUI command flyout, and `/reload` re-runs
discovery.

```sh
clippi --prompt-template ./review.md   # load one template explicitly
clippi --no-prompt-templates            # skip all template discovery
```
