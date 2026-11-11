#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run a layer's own assembly script (guest/layers/{rootssh,devmode,hbchannel}/build-layer.sh) for Meson.

    layer-script.py --dest DIR --stamp FILE --depfile FILE [--dep PATH]... [--dep-tree DIR]...
                    -- COMMAND [ARG]...

These layers are made by scripts because they assemble from other targets' outputs.
COMMAND runs with DEST=DIR; afterwards --stamp gets a hash of DIR (rewritten only when it changes,
so what depends on the layer is skipped when the script made the same tree), and --depfile lists
every --dep (the directory of one that does not exist yet) and every file and directory under
each --dep-tree, so the layer is made again when one of them changes or appears.
"""

import argparse
import os
import subprocess
import sys

from common import manifest, split_command, write_depfile, write_if_changed


def main(argv):
    argv, command = split_command(argv)
    p = argparse.ArgumentParser(
        prog="layer-script.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    p.add_argument("--dest", required=True)
    p.add_argument("--stamp", required=True)
    p.add_argument("--depfile", required=True)
    p.add_argument("--dep", action="append", default=[])
    p.add_argument("--dep-tree", action="append", default=[])
    a = p.parse_args(argv)
    if not command:
        p.error("no COMMAND after --")
    subprocess.run(command, env=dict(os.environ, DEST=os.path.abspath(a.dest)), check=True)
    write_if_changed(a.stamp, manifest(a.dest) + "\n")
    # a missing file: its directory, whose mtime changes when the file appears
    listed = [d if os.path.exists(d) else os.path.dirname(os.path.abspath(d)) for d in a.dep]
    for t in a.dep_tree:
        for dirpath, dirnames, filenames in os.walk(t):
            listed.append(dirpath)
            listed += [os.path.join(dirpath, n) for n in filenames]
    write_depfile(a.depfile, a.stamp, sorted(set(os.path.abspath(p) for p in listed)))


if __name__ == "__main__":
    main(sys.argv[1:])
