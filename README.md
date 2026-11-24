<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/clippi-dark.png">
    <img src="docs/assets/clippi-light.png" alt="Clippi, a pixel-art paper clip with big eyes" width="110">
  </picture>
</p>

<h1 align="center">Clippi</h1>

<p align="center">A tiny, native coding agent for Windows and macOS.</p>

Clippi is a C++23 reimplementation of the core of
[pi](https://github.com/earendil-works/pi) with no JavaScript runtime. A
complete live run, including TLS and a streamed model response, peaks at about
**12 MB of memory**. An empty Node.js process uses about 33 MB before it loads
an agent at all.

> [!NOTE]
> Clippi is in public alpha. Release builds are unsigned previews, so Windows
> SmartScreen or macOS Gatekeeper may warn on first launch.

## Features

- **Small and self-contained.** One static executable with no runtime, package
  manager, or helper tools to install.
- **Full-screen terminal UI** with markdown rendering, a multi-line editor,
  mid-turn steering, and a plain mode for pipes and scripts.
- **Eight built-in tools:** `read`, `write`, `edit`, `ls`, `find`, `grep`,
  `shell`, and `ask_user`. Tool calls in a turn run in parallel.
- **OpenRouter and ChatGPT.** Sign in through the browser or use an API key,
  then pick any model from the provider's live catalog.
- **Durable, branchable sessions** with resume, fork, and automatic compaction.
- **Permission modes:** Ask before every change, Auto-approve routine actions,
  or Yolo. External hooks can veto any tool call.
- **Project context:** `AGENTS.md` / `CLAUDE.md`, [Agent Skills](https://agentskills.io),
  and prompt templates.
- **Memory wiki:** a per-repository knowledge base that carries what the agent
  learned from one session to the next.
- **Embeddable core SDK** with no third-party dependencies.

## Install

**Windows 10+** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/rodrigonso/Clippi/main/tools/install/install.ps1 | iex
```

**macOS 15+** (Terminal):

```sh
curl -fsSL https://raw.githubusercontent.com/rodrigonso/Clippi/main/tools/install/install.sh | sh
```

The installer downloads the latest release, verifies its SHA-256 checksum, and
puts `clippi` on your `PATH`. No administrator rights are needed. Set
`CLIPPI_INSTALL_VERSION=X.Y.Z` to install a specific release, or
`CLIPPI_INSTALL_DIR` to choose the location. If no release is published yet,
[build from source](#building-from-source).

Upgrade with `clippi upgrade`. To uninstall, delete
`%LOCALAPPDATA%\Programs\clippi` on Windows (and remove its `bin` folder from
your user `PATH`), or `~/.local/share/clippi` and `~/.local/bin/clippi` on
macOS. Settings, credentials, and sessions live in `~/.clippi`.

## Quick start

```sh
cd path/to/your/project
clippi --permission-mode ask
```

Run `/auth` to sign in to OpenRouter or ChatGPT (or set `OPENROUTER_API_KEY`),
then choose a model with `/model`. Type `/` to see every command.

```sh
clippi -p "explain the build system"             # one prompt, then exit
clippi --continue                                # resume the last session here
clippi --mode json -p "list the TODOs" > out.jsonl   # NDJSON event stream
clippi -t read,ls,grep                           # read-only tool set
clippi --help                                    # all options
```

## Configuration

| Path | Purpose |
|---|---|
| `~/.clippi/settings.json` | Provider, model, permissions, compaction, retries |
| `~/.clippi/auth.json` | Stored credentials |
| `~/.clippi/hooks.json` | [Lifecycle hooks](docs/HOOKS.md) |
| `AGENTS.md`, `CLAUDE.md` | Project instructions, global and per directory |
| `.agents/skills/`, `~/.clippi/skills/` | [Skills](docs/SKILLS.md) |
| `~/.clippi/prompts/` | [Prompt templates](docs/SKILLS.md#prompt-templates) |

## Documentation

- [Usage](docs/USAGE.md): command line, tools, TUI, JSON mode
- [Configuration](docs/CONFIGURATION.md): prompts, project trust, providers,
  authentication, compaction, retries
- [Permissions](docs/PERMISSIONS.md): Ask, Auto, and Yolo modes and the Auto
  reviewer
- [Lifecycle hooks](docs/HOOKS.md): external commands and the `PreToolUse` gate
- [Memory wiki](docs/MEMORY.md): per-repository memory and dreaming
- [Skills and prompt templates](docs/SKILLS.md)
- [Core SDK](docs/SDK.md): embedding the agent loop
- [Architecture](docs/ARCHITECTURE.md), [UI library](docs/UI.md),
  [Development](docs/DEVELOPMENT.md), [CI](docs/CI.md),
  [Releasing](docs/RELEASING.md)

## Building from source

Requires CMake 3.24+ and a C++23 toolchain: Visual Studio with the Desktop
development with C++ workload on Windows, or Clang with Make or Ninja on macOS.

```powershell
.\tools\build.ps1 -Test      # Windows: build and run the offline test suite
```

```sh
bash tools/build.sh --test   # macOS
```

The executable is written to `build/`. See [Development](docs/DEVELOPMENT.md)
for build options, the dependency-free core SDK, and packaging.

## Safety

Clippi reads and modifies files and runs shell commands with your user's
permissions, and sends conversation and tool context to the selected model
provider. Permission modes are an approval policy, not a sandbox. Start in Ask
mode and review what the agent proposes.

## License

[MIT](LICENSE). Upstream material retains its own license; see
[third-party notices](THIRD_PARTY_NOTICES.md).
