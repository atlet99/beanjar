#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Copy finished build outputs to where the launchers use them (build/Image, build/layers/<name>,
build/qemu, build/viewer/beanjar-viewer, ...), without disturbing a VM or viewer that is running.

    publish.py --dest ROOT --state FILE [--file SRC REL | --tree SRC REL]...

  --file   ROOT/REL is replaced by a copy of SRC through a rename, so a running program keeps
           its old inode.
  --tree   ROOT/REL becomes a copy of directory SRC: the copy is made next to it and exchanged
           with the old tree in one renameat2(RENAME_EXCHANGE); the old tree is kept in
           ROOT/.publish-old/REL until the next time REL changes (a QEMU running from build/qemu
           keeps its files).

An item is only copied when it differs (names, types, modes, link targets, contents). --state
records the size/mtime/mode of every file of both sides after each publish, so a run where
nothing changed only stats files.
"""

import argparse
import ctypes
import json
import os
import shutil
import stat
import sys

from common import same

RENAME_EXCHANGE = 2
AT_FDCWD = -100
_libc = ctypes.CDLL(None, use_errno=True)


def exchange(a, b):
    fn = getattr(_libc, "renameat2", None)
    if fn is not None:
        if fn(AT_FDCWD, a.encode(), AT_FDCWD, b.encode(), RENAME_EXCHANGE) == 0:
            return
    # No renameat2 (or the filesystem refuses it): two renames, a very short gap.
    tmp = b + ".xchg"
    os.rename(b, tmp)
    os.rename(a, b)
    os.rename(tmp, a)


def snapshot(path):
    """Cheap description: (rel, mode, size, mtime_ns, link) for every entry."""
    if not os.path.lexists(path):
        return None
    st = os.lstat(path)
    if not stat.S_ISDIR(st.st_mode):
        return [[".", st.st_mode, st.st_size, st.st_mtime_ns, None]]
    out = []
    for dirpath, dirnames, filenames in os.walk(path):
        dirnames.sort()
        for n in sorted(dirnames + filenames):
            p = os.path.join(dirpath, n)
            s = os.lstat(p)
            link = os.readlink(p) if stat.S_ISLNK(s.st_mode) else None
            size = s.st_size if stat.S_ISREG(s.st_mode) else 0
            mt = s.st_mtime_ns if stat.S_ISREG(s.st_mode) else 0
            out.append([os.path.relpath(p, path), s.st_mode, size, mt, link])
    return out


class Item(argparse.Action):
    """A --file or --tree, kept in the order given: (kind, SRC, REL)."""

    def __call__(self, parser, namespace, values, option_string=None):
        namespace.items.append((option_string[2:], *values))


def main(argv):
    p = argparse.ArgumentParser(
        prog="publish.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    p.add_argument("--dest", required=True)
    p.add_argument("--state", required=True)
    p.set_defaults(items=[])
    p.add_argument("--file", nargs=2, metavar=("SRC", "REL"), action=Item)
    p.add_argument("--tree", nargs=2, metavar=("SRC", "REL"), action=Item)
    a = p.parse_args(argv)
    dest, state_file, items = a.dest, a.state, a.items
    try:
        with open(state_file) as f:
            state = json.load(f)
    except (FileNotFoundError, ValueError):
        state = {}
    new_state = {}
    for kind, src, rel in items:
        dst = os.path.join(dest, rel)
        if not os.path.lexists(src):
            sys.exit(f"publish.py: missing {src}")
        snap = [snapshot(src), snapshot(dst)]
        key = f"{kind}:{rel}"
        if state.get(key) == snap:
            new_state[key] = snap
            continue
        if not same(src, dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if kind == "file":
                tmp = dst + ".publish-tmp"
                shutil.copy2(src, tmp)
                os.replace(tmp, dst)
            else:
                new = dst + ".publish-new"
                shutil.rmtree(new, ignore_errors=True)
                shutil.copytree(src, new, symlinks=True)
                if os.path.lexists(dst):
                    old = os.path.join(dest, ".publish-old", rel)
                    if os.path.lexists(old):
                        shutil.rmtree(old)
                    os.makedirs(os.path.dirname(old), exist_ok=True)
                    exchange(new, dst)
                    os.rename(new, old)
                else:
                    os.rename(new, dst)
            print(f"published {dst}")
        new_state[key] = [snapshot(src), snapshot(dst)]
    tmp = state_file + ".tmp"
    with open(tmp, "w") as f:
        json.dump(new_state, f)
    os.replace(tmp, state_file)


if __name__ == "__main__":
    main(sys.argv[1:])
