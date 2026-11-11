#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep the unpacked tree of one wrap (subprojects/<name>.wrap) up to date, for a custom target.

    fetch.py --source-root DIR --stamp FILE --depfile FILE NAME -- MESON...

The fetching is Meson's own `MESON subprojects download NAME` (MESON: the Meson running the build,
which may be several words): it downloads source_url into the package cache
(subprojects/packagecache, or $MESON_PACKAGE_CACHE_DIR), checks source_hash, unpacks the tarball
into subprojects/<directory>, copies patch_directory over it and applies diff_files in order. What
Meson leaves out is noticing that an unpacked tree is out of date: an existing directory is left
alone (`meson subprojects update --reset` is manual, and for a tree that no subproject() call
configures there is not even a warning). So the tree records a key, the hash of the wrap and of
every file its patch_directory and diff_files name; when the key differs the tree is deleted and
downloaded again, so a patch is never missing, stale or applied twice.

--stamp is rewritten only when the key changes (custom targets use restat) and --depfile names the
wrap and its patch files, so editing, adding or removing a patch re-runs this and what uses the tree.

Each wrap is locked (subprojects/.<name>.lock, exclusively, as meson/shared.py locks its
directories) for the whole check, delete, download and key write, so builds of several targets at
once wait for each other and then find the key current and do nothing. The key is written last: a
tree without the current key is never taken for done.
"""

import argparse
import configparser
import hashlib
import os
import shutil
import subprocess
import sys

from common import file_hash, lock, split_command, write_depfile, write_if_changed


def main(top, name, stamp, depfile, meson):
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
        key.update(file_hash(f))
    key = key.hexdigest()

    lock(os.path.join(subprojects, f".{name}.lock"))  # held until this process exits
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
    write_depfile(depfile, stamp, inputs)


if __name__ == "__main__":
    argv, meson = split_command(sys.argv[1:])
    p = argparse.ArgumentParser(
        prog="fetch.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--source-root", required=True)
    p.add_argument("--stamp", required=True)
    p.add_argument("--depfile", required=True)
    p.add_argument("name")
    a = p.parse_args(argv)
    if not meson:
        p.error("no MESON command after --")
    main(a.source_root, a.name, a.stamp, a.depfile, meson)
