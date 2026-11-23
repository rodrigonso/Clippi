"""Tool registry: the single source of truth for measure, watch and demo."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from .proc import terminate_group, wait_for_socket

HOME = Path.home()
REPO = Path(__file__).resolve().parents[2]


def _env_path(var: str, default: str) -> str:
    return os.path.expanduser(os.environ.get(var, default))


def _binary(var: str, command: str, fallback: Path) -> str:
    return _env_path(var, shutil.which(command) or str(fallback))


@dataclass
class SetupContext:
    """`env` is mutable and inherited by every session; hooks write private paths here."""
    root: Path              # throwaway per-run root, removed afterwards
    home: Path
    cwd: Path
    env: dict[str, str]
    timeout_s: float
    settle_s: float


@dataclass
class Launch:
    """A daemon must be declared here to be charged in RSS and torn down with the run."""
    argv: list[str]
    extra_pids: list[int] = field(default_factory=list)
    extra_pgids: list[int] = field(default_factory=list)
    settle_s: float | None = None


@dataclass
class Tool:
    name: str
    binary: str
    args: list[str] = field(default_factory=list)
    version_args: list[str] = field(default_factory=lambda: ["--version"])
    env: dict[str, str] = field(default_factory=dict)
    matches: Callable[[int, str, str, str], bool] = lambda pid, exe, cmd, real: exe == real
    prepare: Callable[[str, Path], None] | None = None
    setup: Callable[[SetupContext], Launch] | None = None
    # Launcher -> real image; process matching and install sizing must follow it.
    image: Callable[[str], str] | None = None
    note: str = ""

    @property
    def real_binary(self) -> str:
        resolved = os.path.realpath(self.binary)
        return self.image(resolved) if self.image else resolved

    @property
    def argv(self) -> list[str]:
        return [self.binary, *self.args]

    def available(self) -> bool:
        return os.path.isfile(self.binary) and os.access(self.binary, os.X_OK)


def _node_script_match(script_name: str):
    """pi is a node script: exe is node and argv[0] is rewritten to a short
    name, so neither signal identifies it alone."""

    def match(pid: int, exe: str, cmd: str, real: str) -> bool:
        if os.path.basename(exe) not in ("node", "nodejs"):
            return False
        first = os.path.basename(cmd.split()[0]) if cmd.split() else ""
        return first == script_name or real in cmd

    return match


def _jcode_image(resolved: str) -> str:
    """`jcode` may be an sh launcher that execs a platform image beside it."""
    directory = os.path.dirname(resolved)
    try:
        names = sorted(os.listdir(directory))
    except OSError:
        return resolved
    for name in names:
        if name.startswith("jcode-") and name.endswith(".bin"):
            return os.path.join(directory, name)
    return resolved


def _prepare_claude(home: str, cwd: Path) -> None:
    """Skip the theme picker and folder-trust prompt a fresh HOME would show."""
    cfg = {
        "hasCompletedOnboarding": True,
        "theme": "dark",
        "numStartups": 5,
        "installMethod": "npm",
        "autoUpdates": False,
        "projects": {
            str(cwd): {
                "hasTrustDialogAccepted": True,
                "hasCompletedProjectOnboarding": True,
                "allowedTools": [],
                "history": [],
            }
        },
    }
    with open(os.path.join(home, ".claude.json"), "w") as f:
        json.dump(cfg, f)


def _prepare_pi(home: str, cwd: Path) -> None:
    """Link the real ~/.pi cache so pi's first-run fd/ripgrep download is skipped."""
    real_agent = HOME / ".pi" / "agent"
    if not real_agent.exists():
        return
    os.makedirs(os.path.join(home, ".pi"), exist_ok=True)
    link = os.path.join(home, ".pi", "agent")
    if not os.path.exists(link):
        os.symlink(real_agent, link)


# Global flags go before any subcommand; --no-update keeps the measured version fixed.
_JCODE_FLAGS = ["--no-update", "--no-selfdev"]

# Copied, not symlinked: jcode rejects auth files that are symlinks.
_JCODE_AUTH = ("anthropic-auth.json", "openai-auth.json", "antigravity_oauth.json",
               "gemini_oauth.json", "config.toml")


def _copy_if_present(src: Path, dst: Path) -> None:
    if src.exists() and not dst.exists():
        shutil.copyfile(src, dst)
        dst.chmod(0o600)


def _link_if_present(src: Path, dst: Path) -> None:
    if src.exists() and not dst.exists():
        os.symlink(src, dst)


def _setup_jcode(memory_enabled: bool):
    """Start jcode's daemon ourselves: a self-spawned one is setsid-detached (uncounted),
    outlives the run, and its socket escapes the throwaway HOME."""

    def setup(ctx: SetupContext) -> Launch:
        jcode_home = ctx.root / "jcode-home"
        runtime = ctx.root / "run"
        jcode_home.mkdir(parents=True, exist_ok=True)
        runtime.mkdir(parents=True, exist_ok=True)
        socket_path = runtime / "bench.sock"

        ctx.env.update({
            "JCODE_HOME": str(jcode_home),
            "JCODE_RUNTIME_DIR": str(runtime),
            "JCODE_TEMP_SERVER": "1",
            "JCODE_SERVER_OWNER_PID": str(os.getpid()),
            "JCODE_THEME": "dark",
        })

        real = HOME / ".jcode"
        for name in _JCODE_AUTH:
            _copy_if_present(real / name, jcode_home / name)
        if memory_enabled:
            _link_if_present(real / "models", jcode_home / "models")

        # The server refuses to start without a credential; nothing is ever sent.
        ctx.env.setdefault("ANTHROPIC_API_KEY", "sk-ant-agentbench-placeholder")

        binary = _binary("JCODE_BIN", "jcode", HOME / ".local/bin/jcode")
        server = subprocess.Popen(
            [binary, *_JCODE_FLAGS, "serve", "--socket", str(socket_path)],
            cwd=str(ctx.cwd), env=ctx.env,
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid,
        )
        # setsid makes pid == pgid, as in session.launch.
        if not wait_for_socket(str(socket_path), ctx.timeout_s):
            terminate_group(server.pid)
            raise RuntimeError(f"jcode server did not accept on {socket_path} "
                               f"within {ctx.timeout_s:.0f}s")

        return Launch(
            argv=[binary, *_JCODE_FLAGS, "--socket", str(socket_path)],
            extra_pids=[server.pid],
            extra_pgids=[server.pid],
            # The embedding model is still loading well past the default settle.
            settle_s=5.0 if memory_enabled else None,
        )

    return setup


def registry() -> dict[str, Tool]:
    path = os.environ.get("PATH", "")
    if os.environ.get("NODE_BIN"):
        path = os.path.expanduser(os.environ["NODE_BIN"]) + os.pathsep + path

    jcode_bin = _binary("JCODE_BIN", "jcode", HOME / ".local/bin/jcode")

    return {
        "clippi": Tool(
            name="clippi",
            binary=_binary("CLIPPI_BIN", "clippi", REPO / "build/clippi"),
            version_args=["--help"],
            env={"PATH": path},
        ),
        "jcode": Tool(
            name="jcode",
            binary=jcode_bin,
            args=_JCODE_FLAGS,
            version_args=["version"],
            env={"PATH": path, "JCODE_NO_TELEMETRY": "1", "JCODE_MEMORY_ENABLED": "1"},
            setup=_setup_jcode(True),
            image=_jcode_image,
            note="client/server: measured with its own daemon, in its default "
                 "configuration",
        ),
        "jcode-embed-off": Tool(
            name="jcode-embed-off",
            binary=jcode_bin,
            args=_JCODE_FLAGS,
            version_args=["version"],
            env={"PATH": path, "JCODE_NO_TELEMETRY": "1", "JCODE_MEMORY_ENABLED": "0"},
            setup=_setup_jcode(False),
            image=_jcode_image,
            note="same image as jcode, with the local embedding model off",
        ),
        "pi": Tool(
            name="pi",
            binary=_binary("PI_BIN", "pi", HOME / ".local/npm/bin/pi"),
            env={"PATH": path},
            matches=_node_script_match("pi"),
            prepare=_prepare_pi,
        ),
        "claude": Tool(
            name="claude",
            binary=_binary("CLAUDE_BIN", "claude", HOME / ".local/bin/claude"),
            env={"PATH": path},
            prepare=_prepare_claude,
        ),
        "opencode": Tool(
            name="opencode",
            binary=_binary("OPENCODE_BIN", "opencode", HOME / ".local/bin/opencode"),
            env={"PATH": path},
        ),
    }


DEFAULT_TOOLS = ["clippi", "jcode", "jcode-embed-off", "pi", "claude", "opencode"]

# One jcode only: all jcode sessions share a daemon, so live views would misattribute two.
LIVE_TOOLS = ["clippi", "jcode", "pi", "claude", "opencode"]


def resolve(names: list[str] | None, default: list[str] | None = None) -> list[Tool]:
    all_tools = registry()
    wanted = names or default or DEFAULT_TOOLS
    missing = [n for n in wanted if n not in all_tools]
    if missing:
        known = " ".join(sorted(all_tools))
        raise SystemExit(f"unknown tool(s): {' '.join(missing)}\nknown: {known}")
    return [all_tools[n] for n in wanted]
