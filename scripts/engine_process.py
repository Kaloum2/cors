#!/usr/bin/env python3
"""Spawn/stop cors-engine and lab helpers without leaving orphan process groups.

Use preexec_fn=os.setsid so each child is its own session leader (PID == PGID).
Always stop with os.killpg(), never proc.terminate() alone on a setsid child.
"""

from __future__ import annotations

import atexit
import os
import pty
import select
import signal
import subprocess
import sys
import termios
import time
from typing import Callable, Optional

GRACEFUL_TIMEOUT_SEC = 15.0
HARD_KILL_TIMEOUT_SEC = 5.0

_LAB_PROCESS_PATTERNS = (
    "build/cors-engine",
    "mock_ntrip_caster.py",
    "scripts/cli-sidecar.py",
)


def disable_pty_echo(fd: int) -> None:
    try:
        attrs = termios.tcgetattr(fd)
        attrs[3] &= ~termios.ECHO
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except termios.error:
        pass


def process_group_id(proc: subprocess.Popen) -> int:
    return proc.pid


def signal_process_group(pgid: int, sig: signal.Signals) -> None:
    try:
        os.killpg(pgid, sig)
    except ProcessLookupError:
        pass
    except PermissionError:
        try:
            os.kill(pgid, sig)
        except ProcessLookupError:
            pass


def terminate_process_group(
    proc: Optional[subprocess.Popen],
    *,
    graceful: Optional[Callable[[], None]] = None,
    graceful_wait_sec: float = 8.0,
    term_timeout_sec: float = GRACEFUL_TIMEOUT_SEC,
) -> None:
    """SIGTERM (or graceful hook first) then SIGKILL the whole session."""
    if proc is None or proc.poll() is not None:
        return

    pgid = process_group_id(proc)

    if graceful is not None:
        try:
            graceful()
        except Exception:
            pass
        deadline = time.monotonic() + graceful_wait_sec
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                return
            time.sleep(0.15)

    signal_process_group(pgid, signal.SIGTERM)
    try:
        proc.wait(timeout=term_timeout_sec)
        return
    except subprocess.TimeoutExpired:
        pass

    signal_process_group(pgid, signal.SIGKILL)
    try:
        proc.wait(timeout=HARD_KILL_TIMEOUT_SEC)
    except subprocess.TimeoutExpired:
        pass


def cleanup_stale_lab_processes(*, verbose: bool = False) -> None:
    """Best-effort cleanup of leftover lab processes (ports 8002, 7998, …)."""
    for pattern in _LAB_PROCESS_PATTERNS:
        if verbose:
            print(f"cleanup: pkill -f {pattern}", flush=True)
        try:
            subprocess.run(
                ["pkill", "-f", pattern],
                check=False,
                timeout=5,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except (OSError, subprocess.SubprocessError):
            pass
    time.sleep(0.5)


def _atexit_cleanup() -> None:
    cleanup_stale_lab_processes()


def register_lab_cleanup_at_exit() -> None:
    """Register best-effort lab cleanup when a test script exits."""
    atexit.register(_atexit_cleanup)


class ManagedMockCaster:
    def __init__(self, script: str, cwd: str, startup_delay: float = 1.0) -> None:
        self.script = script
        self.cwd = cwd
        self.startup_delay = startup_delay
        self.proc: Optional[subprocess.Popen] = None

    def __enter__(self) -> ManagedMockCaster:
        self.start()
        return self

    def __exit__(self, *_exc) -> None:
        self.stop()

    def start(self) -> None:
        self.proc = subprocess.Popen(
            [sys.executable, self.script],
            cwd=self.cwd,
            close_fds=True,
            preexec_fn=os.setsid,
        )
        time.sleep(self.startup_delay)
        if self.proc.poll() is not None:
            raise RuntimeError(
                f"mock NTRIP caster failed (port busy?). Free 18002/8002 and retry."
            )

    def stop(self) -> None:
        terminate_process_group(self.proc)
        self.proc = None


class ManagedEngine:
    """cors-engine on a PTY; stops the whole process group on exit."""

    def __init__(
        self,
        engine: str,
        config: str,
        *,
        cwd: str = ".",
        trace: int = 1,
        server_flag: bool = True,
        startup_delay: float = 0.0,
    ) -> None:
        self.engine = engine
        self.config = config
        self.cwd = cwd
        self.trace = trace
        self.server_flag = server_flag
        self.startup_delay = startup_delay
        self.master_fd: Optional[int] = None
        self.proc: Optional[subprocess.Popen] = None

    def __enter__(self) -> ManagedEngine:
        self.start()
        return self

    def __exit__(self, *_exc) -> None:
        self.stop()

    @property
    def master(self) -> int:
        if self.master_fd is None:
            raise RuntimeError("engine not started")
        return self.master_fd

    def start(self) -> None:
        master, slave = pty.openpty()
        disable_pty_echo(slave)
        slave_name = os.ttyname(slave)
        cmd = [self.engine, "-o", self.config, "-t", str(self.trace), "-d", slave_name]
        if self.server_flag:
            cmd.append("-s")
        self.proc = subprocess.Popen(
            cmd,
            cwd=self.cwd,
            close_fds=True,
            preexec_fn=os.setsid,
        )
        os.close(slave)
        self.master_fd = master
        if self.startup_delay > 0:
            time.sleep(self.startup_delay)

    def write(self, data: bytes) -> None:
        os.write(self.master, data)

    def send_command(self, cmd: str) -> None:
        line = cmd if cmd.endswith("\r") else cmd + "\r"
        self.write(line.encode())

    def drain(self, seconds: float = 1.0) -> str:
        out: list[str] = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            r, _, _ = select.select([self.master], [], [], 0.1)
            if not r:
                continue
            try:
                chunk = os.read(self.master, 8192)
            except OSError:
                break
            if not chunk:
                break
            out.append(chunk.decode("utf-8", "replace"))
        return "".join(out)

    def stop(self) -> None:
        def graceful() -> None:
            if self.master_fd is None:
                return
            try:
                self.send_command("shutdown")
            except OSError:
                pass

        terminate_process_group(
            self.proc,
            graceful=graceful,
            graceful_wait_sec=8.0,
        )
        if self.master_fd is not None:
            try:
                os.close(self.master_fd)
            except OSError:
                pass
        self.master_fd = None
        self.proc = None
