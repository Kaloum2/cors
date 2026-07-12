#!/usr/bin/env python3
"""Kill leftover cors-engine / mock caster / sidecar from interrupted lab runs."""

from __future__ import annotations

import argparse

from engine_process import cleanup_stale_lab_processes


def main() -> int:
    parser = argparse.ArgumentParser(description="Free lab ports from orphan processes")
    parser.add_argument(
        "-q", "--quiet", action="store_true", help="no log lines"
    )
    args = parser.parse_args()
    cleanup_stale_lab_processes(verbose=not args.quiet)
    if not args.quiet:
        print("lab cleanup done", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
