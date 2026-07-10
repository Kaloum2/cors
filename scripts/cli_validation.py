#!/usr/bin/env python3
"""Canonical CLI whitelist for cors-engine sidecar (INT-05).

Keep in sync with external bridge validators when integrating downstream.
"""

from __future__ import annotations

import re
from typing import List, Tuple

PROMPT = "cors-engine> "
END_MARKER = "__END__"

# Write commands (CRUD) — exact command name match on first token
WRITE_COMMANDS = frozenset(
    {
        "addsource",
        "delsource",
        "adduser",
        "deluser",
        "addvsta",
        "delvsta",
    }
)

# Read commands (monitor fallback)
READ_COMMANDS = frozenset(
    {
        "showbls",
        "showdtrigs",
        "showvsta",
        "sourceinfo",
    }
)

ALLOWED_COMMANDS = WRITE_COMMANDS | READ_COMMANDS

# Reject shell metacharacters and control chars in the full line
FORBIDDEN = re.compile(r"[;|&$`\n\r\t\x00]")

# Minimum argument counts (command name excluded)
MIN_ARGS: dict[str, int] = {
    "addsource": 9,
    "delsource": 1,
    "adduser": 2,
    "deluser": 1,
    "addvsta": 4,
    "delvsta": 1,
    "showbls": 0,
    "showdtrigs": 0,
    "showvsta": 0,
    "sourceinfo": 1,
}


def split_cli_line(line: str) -> List[str]:
    line = line.strip()
    if not line:
        return []
    return line.split()


def validate_cli_line(line: str) -> Tuple[str, List[str]]:
    """Validate one sidecar command line.

    Returns (command, args) on success.
    Raises ValueError with a short reason on rejection.
    """
    if FORBIDDEN.search(line):
        raise ValueError("forbidden characters in command")

    parts = split_cli_line(line)
    if not parts:
        raise ValueError("empty command")

    cmd = parts[0]
    args = parts[1:]

    if cmd == "PING":
        if args:
            raise ValueError("PING takes no arguments")
        return cmd, args

    if cmd not in ALLOWED_COMMANDS:
        raise ValueError(f"command not allowed: {cmd}")

    need = MIN_ARGS.get(cmd, 0)
    if len(args) < need:
        raise ValueError(f"{cmd} expects at least {need} argument(s)")

    return cmd, args


def is_write_command(cmd: str) -> bool:
    return cmd in WRITE_COMMANDS
