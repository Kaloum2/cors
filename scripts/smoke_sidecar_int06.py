#!/usr/bin/env python3
"""INT-06 smoke: sidecar CLI output stable with mock NTRIP + addsource."""

from __future__ import annotations

import os
import re
import socket
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCRIPTS = os.path.join(ROOT, "scripts")
if SCRIPTS not in sys.path:
    sys.path.insert(0, SCRIPTS)

from cli_validation import END_MARKER
from engine_process import ManagedMockCaster, cleanup_stale_lab_processes, register_lab_cleanup_at_exit, terminate_process_group

register_lab_cleanup_at_exit()

ENGINE = os.path.join(ROOT, "build", "cors-engine")
CONF = os.path.join(ROOT, "test/e2e/cors_e2e.conf")
MOCK = os.path.join(ROOT, "test/e2e/mock_ntrip_caster.py")
SIDECAR = os.path.join(SCRIPTS, "cli-sidecar.py")
SIDECAR_PORT = 7998

ADDSOURCE = (
    "addsource TEST01 127.0.0.1 18002 ntripuser ntrip@test TEST01 "
    "48.8566 2.3522 35.0"
)


def sidecar_request(line: str, timeout: float = 35.0) -> str:
    sock = socket.create_connection(("127.0.0.1", SIDECAR_PORT), timeout=5.0)
    sock.sendall((line.strip() + "\n").encode())
    sock.shutdown(socket.SHUT_WR)
    chunks: list[bytes] = []
    sock.settimeout(timeout)
    try:
        while True:
            part = sock.recv(8192)
            if not part:
                break
            chunks.append(part)
    except socket.timeout:
        pass
    finally:
        sock.close()
    return b"".join(chunks).decode(errors="replace")


def body_before_marker(raw: str) -> str:
    if END_MARKER in raw:
        return raw.split(END_MARKER, 1)[0].strip()
    return raw.strip()


def assert_clean_sidecar_output(label: str, raw: str) -> None:
    if END_MARKER not in raw:
        raise AssertionError(f"{label}: missing {END_MARKER}")
    if raw.startswith("ERROR"):
        raise AssertionError(f"{label}: sidecar error: {raw[:200]}")
    if "\x08" in raw:
        raise AssertionError(f"{label}: PTY backspace echo leaked")
    if re.search(r"(?<!\w)showbls(?!\w)", raw) and "stat=" not in raw:
        # echoed command only
        if len(body_before_marker(raw)) < 8:
            raise AssertionError(f"{label}: response looks like bare echo")


def main() -> int:
    if not os.path.isfile(ENGINE):
        print(f"ERROR: build engine first: {ENGINE}")
        return 1
    if not os.path.isfile(CONF):
        print(f"ERROR: missing config: {CONF}")
        return 1

    cleanup_stale_lab_processes()
    sidecar_env = os.environ.copy()
    sidecar_env["PYTHONPATH"] = SCRIPTS
    sidecar = subprocess.Popen(
        [
            sys.executable,
            SIDECAR,
            "--config",
            CONF,
            "--port",
            str(SIDECAR_PORT),
            "--engine",
            ENGINE,
            "--workdir",
            ROOT,
            "-t",
            "1",
        ],
        cwd=ROOT,
        env=sidecar_env,
        close_fds=True,
        preexec_fn=os.setsid,
    )

    ok = True
    try:
        time.sleep(1.5)
        with ManagedMockCaster(MOCK, ROOT):
            ping = sidecar_request("PING", timeout=5.0)
            assert_clean_sidecar_output("PING", ping)
            if "PONG" not in ping:
                raise AssertionError("PING: expected PONG")
            print("PING: OK")

            add = sidecar_request(ADDSOURCE, timeout=45.0)
            assert_clean_sidecar_output("addsource", add)
            print("addsource: OK")

            time.sleep(12)

            showbls = sidecar_request("showbls", timeout=35.0)
            assert_clean_sidecar_output("showbls", showbls)
            bls_body = body_before_marker(showbls)
            if bls_body and "stat=" not in bls_body and "->" not in bls_body:
                raise AssertionError(f"showbls: unexpected body: {bls_body[:200]!r}")
            print(f"showbls: OK ({len(bls_body)} chars)")

            showd = sidecar_request("showdtrigs", timeout=20.0)
            assert_clean_sidecar_output("showdtrigs", showd)
            d_body = body_before_marker(showd)
            if d_body and not re.search(r"^\s*\d+:", d_body, re.MULTILINE):
                raise AssertionError(f"showdtrigs: unexpected body: {d_body[:200]!r}")
            print(f"showdtrigs: OK ({len(d_body)} chars)")

    except AssertionError as exc:
        ok = False
        print(f"FAIL: {exc}")
    finally:
        terminate_process_group(sidecar)
        cleanup_stale_lab_processes()

    print("=== INT-06 sidecar smoke:", "OK" if ok else "FAIL", "===")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
