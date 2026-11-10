#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The C viewer front ends attach whatever versions a run's vm-info.toml records: versions are
information only, and the display, cursor, video and media protocols identify themselves.

    check-attach.py VIEWER_HEADLESS [BEANJAR_VNC]

A run directory with a vm-info.toml and no sockets: with no [beanjar] table, or with any versions in
it, each front end goes on to connect to the VM's bus (and fails there, as there is no VM): it never
refuses the VM for its versions."""

import os
import shutil
import subprocess
import sys
import tempfile

CASES = (
    ("no [beanjar] record", 'platform = "webOS"\nversion = "4.5"\n'),
    (
        "another minor",
        'version = "4.5"\n\n[beanjar]\nhost_version = "0.99.0"\nguest_version = "0.98.3"\n',
    ),
    ("unknown versions", '[beanjar]\nhost_version = "unknown"\nguest_version = "unknown"\n'),
)


def fail(message):
    print(f"FAIL: {message}")
    sys.exit(1)


def main():
    programs = [a for a in sys.argv[1:] if a]
    if not programs:
        print(__doc__)
        return 2
    tmp = tempfile.mkdtemp(prefix="check-attach-")
    try:
        run_dir = os.path.join(tmp, "tv", "run")
        os.makedirs(run_dir)
        bus = os.path.join(run_dir, "dbus.sock")
        for program in programs:
            name = os.path.basename(program)
            argv = [program, f"unix:path={bus}"] + (
                ["--listen", "127.0.0.1", "--port", "5999"]
                if "vnc" in name
                else ["--seconds", "1", "--no-audio"]
            )
            for what, text in CASES:
                with open(os.path.join(run_dir, "vm-info.toml"), "w") as f:
                    f.write(text)
                r = subprocess.run(
                    argv,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=30,
                    env=dict(os.environ, SDL_VIDEODRIVER="offscreen"),
                )
                # beanjar-vnc's own "beanjar-vnc: GL: ..." line is not a refusal
                said = "\n".join(
                    line for line in r.stdout.splitlines() if not line.startswith(f"{name}: GL:")
                )
                if "Cannot attach" in said or "beanjar" in said.lower():
                    fail(f"{name}, {what}: refused for its versions: {r.stdout}")
                if r.returncode == 0 or "Could not connect" not in r.stdout:
                    fail(
                        f"{name}, {what}: did not go on to the VM's bus: exit {r.returncode}: {r.stdout}"
                    )
                print(f"ok: {name} attaches with {what} (then: no VM, exit {r.returncode})")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
