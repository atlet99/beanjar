#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Select Python sources, including non-executable scripts, from pre-commit's file batch."""

from pathlib import Path
import subprocess
import sys


def main():
    args = sys.argv[1:]
    check = bool(args and args[0] == "--check")
    if check:
        args.pop(0)
    sources = []
    for name in args:
        path = Path(name)
        if path.suffix == ".py":
            sources.append(name)
        elif not path.suffix:
            with path.open("rb") as stream:
                first = stream.readline()
            if first.startswith(b"#!") and b"python" in first:
                sources.append(name)
    if not sources:
        return 0
    command = ["ruff", "format"]
    if check:
        command.append("--check")
    return subprocess.run([*command, "--", *sources]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
