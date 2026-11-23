"""Drive a TUI on a pty, answering the terminal probes it blocks on at startup."""

from __future__ import annotations

import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import termios
import time
from dataclasses import dataclass
from pathlib import Path

ANSI_RE = re.compile(r"\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]|\][^\x1b\x07]*(?:\x07|\x1b\\))")

# Typed, never submitted; not a word so it stands out in a screen dump.
PROBE = "jqx92"
PROBE_INTERVAL_S = 2.0

# Set via TIOCSWINSZ *and* in the CSI 18 t reply: some TUIs ask, ioctl-based ones
# read a bare pty as 0x0. Both must agree.
ROWS, COLS = 24, 80

_QUERIES = [
    (b"\x1b[6n", b"\x1b[1;1R"),
    (b"\x1b[c", b"\x1b[?62;c"),
    (b"\x1b]10;?\x1b\\", b"\x1b]10;rgb:ffff/ffff/ffff\x1b\\"),
    (b"\x1b]11;?\x1b\\", b"\x1b]11;rgb:0000/0000/0000\x1b\\"),
    (b"\x1b]10;?\x07", b"\x1b]10;rgb:ffff/ffff/ffff\x07"),
    (b"\x1b]11;?\x07", b"\x1b]11;rgb:0000/0000/0000\x07"),
    (b"\x1b]4;0;?\x07", b"\x1b]4;0;rgb:0000/0000/0000\x07"),
    (b"\x1b[14t", b"\x1b[4;600;800t"),
    (b"\x1b[16t", b"\x1b[6;16;8t"),
    (b"\x1b[18t", f"\x1b[8;{ROWS};{COLS}t".encode()),
    (b"\x1b[?1016$p", b"\x1b[?1016;1$y"),
    (b"\x1b[?2027$p", b"\x1b[?2027;1$y"),
    (b"\x1b[?2031$p", b"\x1b[?2031;1$y"),
    (b"\x1b[?1004$p", b"\x1b[?1004;1$y"),
    (b"\x1b[?2004$p", b"\x1b[?2004;1$y"),
    (b"\x1b[?2026$p", b"\x1b[?2026;1$y"),
]

# Answered "unsupported": opencode stalls half-drawn if these go unanswered.
_DELIMITED = [
    (b"\x1bP+q", b"\x1b\\", b"\x1bP0+q\x1b\\"),
    (b"\x1b_G", b"\x1b\\", b"\x1b_Gi=31337;ENOTSUP\x1b\\"),
    (b"\x1b]1337;", b"\x07", b""),
]


def answer_queries(fd: int, buf: bytes) -> bytes:
    changed = True
    while changed:
        changed = False
        for query, reply in _QUERIES:
            if query in buf:
                os.write(fd, reply)
                buf = buf.replace(query, b"")
                changed = True
        for prefix, terminator, reply in _DELIMITED:
            start = buf.find(prefix)
            if start < 0:
                continue
            end = buf.find(terminator, start + len(prefix))
            if end < 0:
                continue
            if reply:
                os.write(fd, reply)
            buf = buf[:start] + buf[end + len(terminator):]
            changed = True
    return buf


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text).replace("\r", "\n")


def first_meaningful_line(text: str) -> str | None:
    for raw in text.splitlines():
        line = " ".join(raw.split())
        if not line:
            continue
        if sum(c.isalnum() for c in line) >= 3 and len(line) >= 4:
            return line[:160]
    return None


@dataclass
class Session:
    pid: int
    pgid: int
    fd: int
    visible: bool
    input_ready: bool
    excerpt: str | None
    seconds_to_visible: float | None


def launch(argv: list[str], cwd: Path, env: dict[str, str],
           timeout_s: float, settle_s: float) -> Session:
    """Readiness is echo, not paint: opencode drops input typed while it draws,
    so the probe is re-sent on an interval."""
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    proc = subprocess.Popen(
        argv, cwd=str(cwd), env=env,
        stdin=slave, stdout=slave, stderr=slave,
        preexec_fn=os.setsid,
    )
    os.close(slave)
    os.set_blocking(master, False)
    # setsid makes pid == pgid; read now so an early exit can't race getpgid().
    pgid = proc.pid

    start = time.perf_counter()
    buf = b""
    visible = False
    input_ready = False
    excerpt = None
    visible_at = None
    next_probe = 0.0

    while time.perf_counter() - start < timeout_s:
        readable, _, _ = select.select([master], [], [], 0.05)
        if readable:
            try:
                chunk = os.read(master, 65536)
            except (BlockingIOError, OSError):
                chunk = b""
            if chunk:
                buf += chunk
                buf = answer_queries(master, buf)
                plain = strip_ansi(buf.decode("utf-8", "replace"))
                excerpt = first_meaningful_line(plain) or excerpt
                if excerpt and not visible:
                    visible = True
                    visible_at = time.perf_counter() - start
                if PROBE in plain:
                    input_ready = True
                    break
        now = time.perf_counter()
        if visible and now >= next_probe:
            try:
                os.write(master, PROBE.encode())
            except OSError:
                break
            next_probe = now + PROBE_INTERVAL_S
        if proc.poll() is not None:
            break

    if visible:
        time.sleep(settle_s)

    return Session(
        pid=proc.pid, pgid=pgid, fd=master,
        visible=visible, input_ready=input_ready,
        excerpt=excerpt, seconds_to_visible=visible_at,
    )
