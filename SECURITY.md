# Security policy

## Supported versions

Clippi is in public alpha. Only the latest published release receives fixes.

## Reporting a vulnerability

Please report vulnerabilities privately through GitHub's
[private vulnerability reporting](https://github.com/rodrigonso/Clippi/security/advisories/new)
(**Security → Report a vulnerability**). Do not open a public issue, pull
request, or discussion for a suspected vulnerability.

Include the Clippi version (`clippi --version`), your OS, the permission mode in
use, and the steps or prompt needed to reproduce. Leave out real credentials,
session logs, and other private data; redact them if they are needed to show
the problem.

This is a small project maintained on a best-effort basis. Expect an initial
response within a week. Fixes are released as soon as practical, and reporters
are credited in the advisory unless they prefer otherwise.

## Scope

Clippi runs tools with your user's permissions, and its permission modes are an
approval policy, not a sandbox. Reports are most useful when they show Clippi
doing something it should have refused or asked about, for example:

- a tool call that runs without approval in Ask mode, or that bypasses an
  external `PreToolUse` hook
- an Auto-mode action admitted without review that should not have been, such
  as a `write` or `edit` outside the workspace or under `.git`, `.clippi`, or
  `.agents`
- a `write` or `edit` that reaches a memory wiki's `journal.jsonl` or `.dream/`
- project prompt files, skills, or templates loaded without project trust
- credentials or API keys written to logs, the transcript, the clipboard, or
  anywhere other than `~/.clippi/auth.json`
- a release archive or install script that can be tampered with in a way its
  checksum verification does not catch

Out of scope: actions you approved, anything done in Yolo mode, behavior of the
chosen model or provider, and the fact that release builds are not yet signed.
