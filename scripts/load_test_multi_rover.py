#!/usr/bin/env python3
"""Multi-rover NTRIP agent load probe (TST-05).

Opens N parallel TCP connections to the local NTRIP agent, sends a minimal
GET request, and reports success/failure counts. Lab use only.
"""

from __future__ import annotations

import argparse
import socket
import threading
import time
from typing import List, Tuple

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8002
DEFAULT_USER = "lab"
DEFAULT_PASS = "lab"
DEFAULT_MNT = "RTCM32"


def one_client(host: str, port: int, user: str, passwd: str, mnt: str, idx: int) -> Tuple[int, str]:
    import base64

    cred = base64.b64encode(f"{user}:{passwd}".encode()).decode()
    req = (
        f"GET /{mnt} HTTP/1.0\r\n"
        f"User-Agent: load-test-{idx}\r\n"
        f"Authorization: Basic {cred}\r\n"
        f"\r\n"
    )
    try:
        s = socket.create_connection((host, port), 5)
        s.sendall(req.encode())
        data = s.recv(256)
        s.close()
        if b"200" in data or b"ICY 200" in data:
            return idx, "ok"
        return idx, data[:80].decode(errors="replace")
    except OSError as exc:
        return idx, str(exc)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--host", default=DEFAULT_HOST)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--user", default=DEFAULT_USER)
    p.add_argument("--password", default=DEFAULT_PASS)
    p.add_argument("--mountpoint", default=DEFAULT_MNT)
    p.add_argument("-n", "--clients", type=int, default=10)
    args = p.parse_args()

    results: List[Tuple[int, str]] = []
    lock = threading.Lock()

    def worker(i: int) -> None:
        r = one_client(args.host, args.port, args.user, args.password, args.mountpoint, i)
        with lock:
            results.append(r)

    t0 = time.monotonic()
    threads = [threading.Thread(target=worker, args=(i,)) for i in range(args.clients)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    dt = time.monotonic() - t0

    ok = sum(1 for _, status in results if status == "ok")
    print(f"clients={args.clients} ok={ok} fail={args.clients - ok} elapsed={dt:.2f}s")
    for idx, status in sorted(results):
        if status != "ok":
            print(f"  client {idx}: {status}")
    return 0 if ok == args.clients else 1


if __name__ == "__main__":
    raise SystemExit(main())
