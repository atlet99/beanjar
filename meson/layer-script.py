#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run a layer's own assembly script (guest/layers/{rootssh,devmode,hbchannel}/build-layer.sh) for Meson.

    layer-script.py --dest DIR --stamp FILE --depfile FILE [--dep PATH]... [--dep-tree DIR]...
                    -- COMMAND [ARG]...

These layers are made by scripts because they assemble from other targets' outputs.
COMMAND runs with DEST=DIR; afterwards --stamp gets a hash of DIR, and --depfile lists every --dep
(the directory of one that does not exist yet) and every file and directory under each --dep-tree,
so the layer is made again when one of them changes or appears.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stage import manifest, write_if_changed  # noqa: E402


def main(argv):
    i = argv.index("--")
    opts, cmd = argv[:i], argv[i + 1 :]
    dest = stamp = depfile = None
    deps, trees = [], []
    j = 0
    while j < len(opts):
        k, v = opts[j], opts[j + 1]
        j += 2
        if k == "--dest":
            dest = v
        elif k == "--stamp":
            stamp = v
        elif k == "--depfile":
            depfile = v
        elif k == "--dep":
            deps.append(v)
        elif k == "--dep-tree":
            trees.append(v)
        else:
            sys.exit(f"layer-script.py: unknown {k}")
    env = dict(os.environ, DEST=os.path.abspath(dest))
    subprocess.run(cmd, env=env, check=True)
    write_if_changed(stamp, manifest(dest) + "\n")
    listed = []
    for d in deps:
        # a missing file: its directory, whose mtime changes when the file appears
        listed.append(
            os.path.abspath(d if os.path.exists(d) else os.path.dirname(os.path.abspath(d)))
        )
    for t in trees:
        for dirpath, dirnames, filenames in os.walk(t):
            listed.append(os.path.abspath(dirpath))
            listed += [os.path.abspath(os.path.join(dirpath, n)) for n in filenames]
    listed = sorted(set(listed))
    write_if_changed(depfile, stamp + ": " + " ".join(p.replace(" ", "\\ ") for p in listed) + "\n")


if __name__ == "__main__":
    main(sys.argv[1:])
