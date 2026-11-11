#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep the unpacked tree of one wrap (subprojects/<name>.wrap) up to date, for a custom target.

    fetch.py MESON SOURCE_ROOT NAME STAMP DEPFILE

The fetching is Meson's own `meson subprojects download NAME`: it downloads source_url into the
package cache (subprojects/packagecache, or $MESON_PACKAGE_CACHE_DIR), checks source_hash,
unpacks the tarball into subprojects/<directory>, copies patch_directory over it and applies
diff_files in order. What Meson leaves out is noticing that an unpacked tree is out of date: an
existing directory is left alone (`meson subprojects update --reset` is manual, and for a tree
that no subproject() call configures there is not even a warning). So the tree records a key, the
hash of the wrap and of every file its patch_directory and diff_files name; when the key differs
the tree is deleted and downloaded again, so a patch is never missing, stale or applied twice.

STAMP is rewritten only when the key changes (custom targets use restat) and DEPFILE names the
wrap and its patch files, so editing, adding or removing a patch re-runs this and what uses the tree.

Each wrap is locked (subprojects/.<name>.lock, exclusively, like meson/shared.py) for the whole
check, delete, download and key write, so builds of several targets at once wait for each other
and then find the key current and do nothing. The key is written last: a tree without the current
key is never taken for done.
"""

import configparser
import fcntl
import hashlib
import os
import shutil
import subprocess
import sys


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


def main(meson, top, name, stamp, depfile):
    subprojects = os.path.join(top, "subprojects")
    wrap_path = os.path.join(subprojects, name + ".wrap")
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(wrap_path)
    w = cp["wrap-file"]
    pkgdir = os.path.join(subprojects, "packagefiles")
    inputs = [wrap_path]
    if w.get("patch_directory"):
        for dirpath, dirnames, filenames in os.walk(os.path.join(pkgdir, w["patch_directory"])):
            dirnames.sort()
            inputs += [os.path.join(dirpath, f) for f in sorted(filenames)]
    inputs += [
        os.path.join(pkgdir, d.strip()) for d in w.get("diff_files", "").split(",") if d.strip()
    ]

    key = hashlib.sha256()
    for f in inputs:
        key.update(os.path.relpath(f, subprojects).encode() + b"\0")
        with open(f, "rb") as fh:
            key.update(hashlib.sha256(fh.read()).digest())
    key = key.hexdigest()

    lockfd = os.open(os.path.join(subprojects, f".{name}.lock"), os.O_RDWR | os.O_CREAT, 0o644)
    fcntl.flock(lockfd, fcntl.LOCK_EX)  # released when this process exits
    tree = os.path.join(subprojects, w["directory"])
    keyfile = os.path.join(tree, ".wrap-key")
    try:
        with open(keyfile) as f:
            current = f.read().strip()
    except FileNotFoundError:
        current = None
    if current != key:
        if os.path.lexists(tree):
            print(
                f"{name}: the wrap or its patches changed, unpacking {w['directory']} again",
                file=sys.stderr,
            )
            shutil.rmtree(tree)
        r = subprocess.run(
            meson + ["subprojects", "download", "--sourcedir", top, name],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        # `meson subprojects download` reports a failure in its output, not always its status.
        if r.returncode != 0 or not os.path.isdir(tree) or " -> done" not in r.stdout:
            sys.exit(r.stdout + f"\nfetch.py: could not download {name}")
        with open(keyfile, "w") as f:
            f.write(key + "\n")

    write_if_changed(stamp, key + "\n")
    deps = " ".join(os.path.abspath(p).replace(" ", "\\ ") for p in inputs)
    write_if_changed(depfile, f"{stamp}: {deps}\n")


if __name__ == "__main__":
    # MESON may be several words (python3 /path/meson.py): all but the last four arguments.
    a = sys.argv[1:]
    if len(a) < 5:
        sys.exit(__doc__)
    main(a[:-4], *a[-4:])
