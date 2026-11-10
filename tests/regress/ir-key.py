#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tap LG remote buttons on a running VM, through its ir.sock (webos.ir -> the micom model), as the
viewer's remote does (tests/regress/input/check-ir.py checks that path):

    ir-key.py [--dir VM-DIR | --sock IR_SOCK] [--local] [--hold SECONDS] [CODE...]

The socket is the checkout VM's (RUN_DIR/ir.sock, RUN_DIR default build), a library VM's with
--dir (VM-DIR/run/ir.sock, as host/scripts/vm-launcher --dir), or --sock IR_SOCK. The default code is HOME
(0x7C); codes are LG IR data codes, custom 0x04. ir.sock takes one client: no viewer may be
attached. --local presses the set's own key instead (include/webos-ir.h WEBOS_LOCAL_MAGIC; the micom
times it: 0xEA, the one key, is a short press held less than lgemu's local_long_ms, 1 s, a long
one held longer); --hold is how long each is held (default 0.15 s).
"""

import argparse
import os
import socket
import struct
import sys
import time

TOP = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))


def record(down, code, local=False):
    if local:
        return struct.pack("4B", 0x4B, 1 if down else 0, 0, code)
    return struct.pack("4B", 0x49, 1 if down else 0, 0x04, code)


def sock_path(a, env=os.environ):
    if a.dir:
        return os.path.join(os.path.abspath(a.dir), "run", "ir.sock")
    return a.sock or os.path.join(TOP, env.get("RUN_DIR") or "build", "ir.sock")


def main(argv):
    p = argparse.ArgumentParser(
        prog="ir-key.py", description="tap LG remote buttons over a VM's ir.sock"
    )
    where = p.add_mutually_exclusive_group()
    where.add_argument("--dir", help="a VM directory (the manager's layout): its run/ir.sock")
    where.add_argument("--sock", help="the ir.sock (default: the checkout VM's)")
    p.add_argument(
        "--local", action="store_true", help="the set's own key: micom local key codes (0xEA)"
    )
    p.add_argument("--hold", type=float, default=0.15, help="seconds each is held (default 0.15)")
    p.add_argument(
        "codes", nargs="*", type=lambda c: int(c, 0), help="LG IR data codes (default 0x7C, HOME)"
    )
    a = p.parse_args(argv)
    path = sock_path(a)
    with socket.socket(socket.AF_UNIX) as s:
        try:
            s.connect(path)
        except OSError as e:
            print(f"ir-key.py: {path}: {e.strerror}", file=sys.stderr)
            return 1
        for code in a.codes or [0xEA if a.local else 0x7C]:
            s.sendall(record(True, code, a.local))
            time.sleep(a.hold)
            s.sendall(record(False, code, a.local))
            time.sleep(0.45)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
