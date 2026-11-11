#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build something once for every guest build directory that would build it the same.

    shared.py key LINE...
        the key of a shared directory: a hash of the lines (what the builds that share it have
        alike: compilers, flags, source trees, options), printed for meson.build
    shared.py run --dir DIR [--after DIR]... [--input FILE...]... [--tool PROGRAM]...
                  [--file SRC DEST]... [--tree SRC DEST]... [--depfile FILE] -- COMMAND...
        COMMAND builds into DIR (<shared_dir>/<component>/<key>); then each SRC in DIR is
        copied to DEST in the build directory, replacing it only when it differs

COMMAND is skipped when DIR holds the result of the same inputs already: the contents of each
--input file (the source tree's wrap key, a config, the build script, the sources), each --tool's
`--version` output (the compiler), COMMAND itself and the inputs of each --after directory (one
this is built against, such as the kernel's object tree for a module). That hash is DIR/.inputs,
written after COMMAND succeeds and removed before it starts. A changed input runs COMMAND again in
the same directory, where its own build system (make, Kbuild) is incremental.

DIR is locked while this runs (DIR/.lock, exclusively) and each --after directory too (shared),
always in the order given and before DIR, so builds of several targets at once wait for each
other and find the work done.

--depfile names DIR/.inputs and those of the --after directories, so a build directory notices
that another one changed DIR, or that it was removed; a DEST older than them is touched, since a
rule with a depfile has no restat.
"""

import argparse
import fcntl
import hashlib
import os
import shutil
import subprocess
import sys

from common import file_hash, lock, same, split_command, write_depfile


def key(lines):
    return hashlib.sha256("".join(line + "\n" for line in lines).encode()).hexdigest()[:16]


def parse(argv):
    argv, command = split_command(argv)
    p = argparse.ArgumentParser(prog="shared.py run", allow_abbrev=False)
    p.add_argument("--dir", required=True)
    p.add_argument("--after", action="append", default=[])
    p.add_argument("--input", action="extend", nargs="+", default=[])
    p.add_argument("--tool", action="append", default=[])
    p.add_argument("--file", action="append", nargs=2, default=[])
    p.add_argument("--tree", action="append", nargs=2, default=[])
    p.add_argument("--depfile")
    opts = p.parse_args(argv)
    if not command:
        p.error("no COMMAND after --")
    return opts, command


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except FileNotFoundError:
        return None


def digest(opts, command):
    h = hashlib.sha256()
    h.update(("\0".join(command) + "\0\0").encode())
    for after in opts.after:
        inputs = read(os.path.join(after, ".inputs"))
        if inputs is None:
            sys.exit(f"shared.py: {after} has not been built (or was removed): build again")
        h.update(f"after {after} {inputs}".encode())
    for path in opts.input:
        h.update(f"input {path} ".encode() + file_hash(path))
    for tool in opts.tool:
        out = subprocess.run([tool, "--version"], stdout=subprocess.PIPE, check=True).stdout
        h.update(f"tool {shutil.which(tool)} ".encode() + out)
    return h.hexdigest() + "\n"


def copy_file(src, dest):
    """DEST becomes a copy of file SRC (a link followed) through a rename, unless it is one."""
    if same(os.path.realpath(src), dest):
        return
    shutil.copy2(src, dest + ".tmp")
    os.replace(dest + ".tmp", dest)


def copy_tree(src, dest):
    """DEST becomes a copy of tree SRC (links kept) through a rename, unless it is one."""
    if same(src, dest):
        return
    tmp = dest + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.copytree(src, tmp, symlinks=True)
    if os.path.lexists(dest):
        shutil.rmtree(dest)
    os.replace(tmp, dest)


def lock_dir(directory, mode):
    os.makedirs(directory, exist_ok=True)
    return lock(os.path.join(directory, ".lock"), mode)


def run(opts, command):
    directory = opts.dir
    fds = [lock_dir(after, fcntl.LOCK_SH) for after in opts.after]
    fds.append(lock_dir(directory, fcntl.LOCK_EX))
    stamp = os.path.join(directory, ".inputs")
    want = digest(opts, command)
    copies = opts.file + opts.tree
    if read(stamp) != want or not all(os.path.lexists(src) for src, _ in copies):
        if os.path.lexists(stamp):
            os.remove(stamp)
        r = subprocess.run(command)
        if r.returncode != 0:
            sys.exit(r.returncode)
        with open(stamp + ".tmp", "w") as f:
            f.write(want)
        os.replace(stamp + ".tmp", stamp)
    for src, dest in opts.file:
        copy_file(src, dest)
    for src, dest in opts.tree:
        copy_tree(src, dest)
    if opts.depfile:
        stamps = [stamp] + [os.path.join(after, ".inputs") for after in opts.after]
        newest = max(os.stat(s).st_mtime_ns for s in stamps)
        for _, dest in copies:
            if os.stat(dest).st_mtime_ns < newest:
                os.utime(dest)
        write_depfile(opts.depfile, copies[0][1], stamps)
    for fd in fds:
        os.close(fd)


def main(argv):
    if argv[:1] == ["key"]:
        print(key(argv[1:]))
    elif argv[:1] == ["run"]:
        run(*parse(argv[1:]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
