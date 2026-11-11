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

import fcntl
import hashlib
import os
import shutil
import subprocess
import sys


def key(lines):
    return hashlib.sha256("".join(line + "\n" for line in lines).encode()).hexdigest()[:16]


def parse(argv):
    opts = {
        "dir": None,
        "after": [],
        "input": [],
        "tool": [],
        "file": [],
        "tree": [],
        "depfile": None,
    }
    i = 0
    while i < len(argv) and argv[i] != "--":
        a = argv[i]
        name = a[2:]
        if a in ("--dir", "--depfile"):
            opts[name] = argv[i + 1]
            i += 2
        elif a in ("--after", "--tool"):
            opts[name].append(argv[i + 1])
            i += 2
        elif a == "--input":  # --input FILE..., up to the next option
            i += 1
            while i < len(argv) and not argv[i].startswith("--"):
                opts[name].append(argv[i])
                i += 1
        elif a in ("--file", "--tree"):
            opts[name].append((argv[i + 1], argv[i + 2]))
            i += 3
        else:
            sys.exit(f"shared.py: unknown argument {a}")
    command = argv[i + 1 :]
    if not opts["dir"] or not command:
        sys.exit(__doc__)
    return opts, command


def lock(directory, mode):
    os.makedirs(directory, exist_ok=True)
    fd = os.open(os.path.join(directory, ".lock"), os.O_RDWR | os.O_CREAT, 0o644)
    fcntl.flock(fd, mode)
    return fd


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except FileNotFoundError:
        return None


def digest(opts, command):
    h = hashlib.sha256()
    h.update(("\0".join(command) + "\0\0").encode())
    for after in opts["after"]:
        inputs = read(os.path.join(after, ".inputs"))
        if inputs is None:
            sys.exit(f"shared.py: {after} has not been built (or was removed): build again")
        h.update(f"after {after} {inputs}".encode())
    for path in opts["input"]:
        with open(path, "rb") as f:
            h.update(f"input {path} ".encode() + hashlib.sha256(f.read()).digest())
    for tool in opts["tool"]:
        out = subprocess.run([tool, "--version"], stdout=subprocess.PIPE, check=True).stdout
        h.update(f"tool {shutil.which(tool)} ".encode() + out)
    return h.hexdigest() + "\n"


def same_tree(a, b):
    """Whether directory trees a and b have the same names, types, modes, links and contents."""
    if not os.path.isdir(b):
        return False
    for root, dirs, files in os.walk(a):
        rel = os.path.relpath(root, a)
        other = os.path.join(b, rel)
        if sorted(dirs + files) != sorted(os.listdir(other)):
            return False
        for name in dirs + files:
            p, q = os.path.join(root, name), os.path.join(other, name)
            sp, sq = os.lstat(p), os.lstat(q)
            if sp.st_mode != sq.st_mode:
                return False
            if os.path.islink(p):
                if os.readlink(p) != os.readlink(q):
                    return False
            elif os.path.isfile(p) and not same_file(p, q):
                return False
    return True


def same_file(a, b):
    if not os.path.isfile(b) or os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            x, y = fa.read(1 << 20), fb.read(1 << 20)
            if x != y:
                return False
            if not x:
                return True


def copy_file(src, dest):
    if same_file(src, dest):
        return
    tmp = dest + ".tmp"
    shutil.copy2(src, tmp)
    os.replace(tmp, dest)


def copy_tree(src, dest):
    if same_tree(src, dest):
        return
    tmp = dest + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.copytree(src, tmp, symlinks=True)
    if os.path.lexists(dest):
        shutil.rmtree(dest)
    os.replace(tmp, dest)


def run(opts, command):
    directory = opts["dir"]
    fds = [lock(after, fcntl.LOCK_SH) for after in opts["after"]]
    fds.append(lock(directory, fcntl.LOCK_EX))
    stamp = os.path.join(directory, ".inputs")
    want = digest(opts, command)
    outputs = [src for src, _ in opts["file"] + opts["tree"]]
    if read(stamp) != want or not all(os.path.lexists(src) for src in outputs):
        if os.path.lexists(stamp):
            os.remove(stamp)
        r = subprocess.run(command)
        if r.returncode != 0:
            sys.exit(r.returncode)
        with open(stamp + ".tmp", "w") as f:
            f.write(want)
        os.replace(stamp + ".tmp", stamp)
    for src, dest in opts["file"]:
        copy_file(src, dest)
    for src, dest in opts["tree"]:
        copy_tree(src, dest)
    if opts["depfile"]:
        stamps = [stamp] + [os.path.join(after, ".inputs") for after in opts["after"]]
        newest = max(os.stat(s).st_mtime_ns for s in stamps)
        dests = [dest for _, dest in opts["file"] + opts["tree"]]
        for dest in dests:
            if os.stat(dest).st_mtime_ns < newest:
                os.utime(dest)
        with open(opts["depfile"], "w") as f:
            f.write(
                dests[0].replace(" ", "\\ ")
                + ": "
                + " ".join(s.replace(" ", "\\ ") for s in stamps)
                + "\n"
            )
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
