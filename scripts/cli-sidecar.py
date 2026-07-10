#!/usr/bin/env python3
"""TCP CLI sidecar for cors-engine (INT-02).

Listens on :7998, forwards whitelisted commands to a single cors-engine PTY.
Protocol: one line in → engine output until prompt → response + __END__.
PING → PONG + __END__ (health check, no engine round-trip).
"""

from __future__ import annotations

import argparse
import os
import pty
import select
import signal
import socket
import subprocess
import sys
import threading
import time
from typing import Optional

from cli_validation import END_MARKER, PROMPT, validate_cli_line

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 7998
ENGINE_TIMEOUT_SEC = 30.0
READ_CHUNK = 4096


class EngineSession:
    """Single cors-engine process on a PTY."""

    def __init__(self, engine: str, config: str, trace: int, workdir: str) -> None:
        self.engine = engine
        self.config = config
        self.trace = trace
        self.workdir = workdir
        self._master_fd: Optional[int] = None
        self._proc: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._buf = ""

    def start(self) -> None:
        master, slave = pty.openpty()
        cmd = [self.engine, "-o", self.config, "-t", str(self.trace), "-s"]
        self._proc = subprocess.Popen(
            cmd,
            stdin=slave,
            stdout=slave,
            stderr=slave,
            cwd=self.workdir,
            close_fds=True,
        )
        os.close(slave)
        self._master_fd = master
        self._wait_prompt(ENGINE_TIMEOUT_SEC)

    def stop(self) -> None:
        with self._lock:
            if self._proc and self._proc.poll() is None:
                try:
                    self._write_unlocked("stop\n")
                    self._drain_until_prompt(5.0)
                except Exception:
                    pass
                self._proc.terminate()
                try:
                    self._proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self._proc.kill()
            if self._master_fd is not None:
                os.close(self._master_fd)
                self._master_fd = None
            self._proc = None
            self._buf = ""

    def _read_available(self, timeout: float) -> str:
        if self._master_fd is None:
            return ""
        out = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r, _, _ = select.select([self._master_fd], [], [], 0.1)
            if not r:
                continue
            try:
                chunk = os.read(self._master_fd, READ_CHUNK)
            except OSError:
                break
            if not chunk:
                break
            out.append(chunk.decode(errors="replace"))
        return "".join(out)

    def _drain_until_prompt(self, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._buf += self._read_available(0.2)
            if PROMPT in self._buf:
                idx = self._buf.rfind(PROMPT)
                resp = self._buf[:idx]
                self._buf = self._buf[idx + len(PROMPT) :]
                return resp
            if self._proc and self._proc.poll() is not None:
                raise RuntimeError("cors-engine exited unexpectedly")
        raise TimeoutError("engine prompt timeout")

    def _wait_prompt(self, timeout: float) -> None:
        self._drain_until_prompt(timeout)

    def _write_unlocked(self, data: str) -> None:
        if self._master_fd is None:
            raise RuntimeError("engine not started")
        os.write(self._master_fd, data.encode())

    def run_command(self, line: str) -> str:
        with self._lock:
            validate_cli_line(line)
            if self._proc is None or self._proc.poll() is not None:
                self.start()
            self._write_unlocked(line.strip() + "\n")
            return self._drain_until_prompt(ENGINE_TIMEOUT_SEC)


class SidecarServer:
    def __init__(self, host: str, port: int, session: EngineSession) -> None:
        self.host = host
        self.port = port
        self.session = session
        self._sock: Optional[socket.socket] = None
        self._stop = threading.Event()

    def serve_forever(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.host, self.port))
        self._sock.listen(8)
        self._sock.settimeout(1.0)
        print(f"cli-sidecar listening on {self.host}:{self.port}", flush=True)
        while not self._stop.is_set():
            try:
                conn, addr = self._sock.accept()
            except socket.timeout:
                continue
            threading.Thread(target=self._handle_client, args=(conn, addr), daemon=True).start()

    def shutdown(self) -> None:
        self._stop.set()
        if self._sock:
            self._sock.close()

    def _handle_client(self, conn: socket.socket, addr) -> None:
        try:
            conn.settimeout(ENGINE_TIMEOUT_SEC + 5)
            data = conn.recv(8192)
            if not data:
                return
            line = data.decode(errors="replace").strip()
            if not line:
                conn.sendall(b"ERROR empty line\n" + END_MARKER.encode() + b"\n")
                return
            if line == "PING":
                conn.sendall(b"PONG\n" + END_MARKER.encode() + b"\n")
                return
            try:
                out = self.session.run_command(line)
                conn.sendall(out.encode(errors="replace"))
                if not out.endswith("\n"):
                    conn.sendall(b"\n")
                conn.sendall(END_MARKER.encode() + b"\n")
            except ValueError as exc:
                conn.sendall(f"ERROR {exc}\n".encode() + END_MARKER.encode() + b"\n")
            except (TimeoutError, RuntimeError) as exc:
                conn.sendall(f"ERROR {exc}\n".encode() + END_MARKER.encode() + b"\n")
                self.session.stop()
        finally:
            conn.close()


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="cors-engine CLI sidecar")
    p.add_argument("--host", default=os.environ.get("CORS_SIDECAR_HOST", DEFAULT_HOST))
    p.add_argument("--port", type=int, default=int(os.environ.get("CORS_SIDECAR_PORT", DEFAULT_PORT)))
    p.add_argument("--engine", default=os.environ.get("CORS_ENGINE_BIN", "build/cors-engine"))
    p.add_argument("--config", default=os.environ.get("CORS_CONF", "conf/cors.conf"))
    p.add_argument("--workdir", default=os.environ.get("CORS_WORKDIR", "."))
    p.add_argument("-t", "--trace", type=int, default=1)
    return p.parse_args()


def main() -> int:
    args = parse_args()
    session = EngineSession(args.engine, args.config, args.trace, args.workdir)
    server = SidecarServer(args.host, args.port, session)

    def on_signal(_sig, _frame):
        server.shutdown()
        session.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    try:
        server.serve_forever()
    finally:
        session.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
