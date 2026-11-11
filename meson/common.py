# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the build's Python helpers in meson/ share; not run by itself.

write_if_changed  a stamp or depfile rewritten only when its text changes: Meson's custom targets
                  run with restat, so an unchanged stamp lets Ninja skip what depends on it
write_depfile     a Makefile-style depfile naming a target's inputs
lock              an exclusive or shared flock(2) on a lock file, held until the process exits
                  or the descriptor is closed, so builds of several targets at once wait for
                  each other
file_hash         the SHA-256 of a file's contents
manifest          a hash of a tree: names, types, modes, link targets and contents
same              whether two files or trees have the same names, types, modes, link targets and
                  contents
split_command     the options before `--` and the command after it
"""

import fcntl
import filecmp
import hashlib
import os
import stat


def write_if_changed(path, text):
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(path + ".tmp", "w") as f:
        f.write(text)
    os.replace(path + ".tmp", path)


def _escape(path):
    return path.replace(" ", "\\ ")


def write_depfile(path, target, deps):
    """TARGET (as the rule names its output) depends on each of deps (made absolute)."""
    text = _escape(target) + ": " + " ".join(_escape(os.path.abspath(d)) for d in deps) + "\n"
    write_if_changed(path, text)


def lock(path, mode=fcntl.LOCK_EX):
    """A descriptor of the lock file at path (created), locked with mode."""
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
    fcntl.flock(fd, mode)
    return fd


def file_hash(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").digest()


def manifest(root):
    h = hashlib.sha256()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(dirnames + filenames):
            p = os.path.join(dirpath, name)
            st = os.lstat(p)
            rel = os.path.relpath(p, root)
            h.update(f"{rel}\0{st.st_mode:o}\0".encode())
            if stat.S_ISLNK(st.st_mode):
                h.update(os.readlink(p).encode())
            elif stat.S_ISREG(st.st_mode):
                h.update(file_hash(p))
            h.update(b"\n")
    return h.hexdigest()


def same(a, b):
    if not os.path.lexists(b):
        return False
    sa, sb = os.lstat(a), os.lstat(b)
    if sa.st_mode != sb.st_mode:
        return False
    if stat.S_ISLNK(sa.st_mode):
        return os.readlink(a) == os.readlink(b)
    if stat.S_ISREG(sa.st_mode):
        return sa.st_size == sb.st_size and filecmp.cmp(a, b, shallow=False)
    if stat.S_ISDIR(sa.st_mode):
        names = sorted(os.listdir(a))
        return names == sorted(os.listdir(b)) and all(
            same(os.path.join(a, n), os.path.join(b, n)) for n in names
        )
    return True


def split_command(argv):
    """(the arguments before the first `--`, those after it); no `--`: (argv, [])."""
    if "--" not in argv:
        return argv, []
    i = argv.index("--")
    return argv[:i], argv[i + 1 :]
