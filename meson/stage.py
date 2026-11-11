#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Assemble a directory tree (a layer, the test programs) from sources and build outputs.

    stage.py --out DIR --stamp FILE --depfile FILE [--strip STRIP] [--readelf READELF
             --glibc-max VERSION] OP...
    stage.py --manifest DIR STAMP      (only the stamp, for a tree built by something else)

OPs, applied in order:
    --tree SRC DEST          copy directory SRC into DEST (relative to DIR; '.' for the root),
                             keeping mtimes and symlinks; modes are normalised (directories
                             755, files 644, or 755 with any x bit), so the checkout's umask
                             does not reach the layer; a meson.build in it (the module's build
                             description) is not copied
    --built-tree SRC DEST    as --tree, for a tree made by another target whose stamp the
                             caller depends on: its files are not listed in the depfile
    --file SRC DEST MODE     install file SRC as DEST with MODE (octal)
    --link TARGET DEST       symlink DEST -> TARGET
    --dir DEST MODE          directory DEST with MODE

The tree is built next to DIR and swapped in, so DIR is never half-written. Then:
  --strip       every ELF file is stripped with STRIP --strip-unneeded;
  --glibc-max   no ELF file may need a GLIBC_ symbol version newer than VERSION (the firmware's
                glibc 2.14.1): the build fails instead of the guest's dynamic linker.
--stamp holds a hash of the result (names, types, modes, link targets, contents) and is only
rewritten when that changes, so the steps after it (the initramfs, publishing) re-run only then.
--depfile lists every input, including the directories of --tree sources, so a file added to a
source tree is noticed too.
"""

import argparse
import os
import re
import shutil
import stat
import subprocess
import sys

from common import manifest, write_depfile, write_if_changed


class Op(argparse.Action):
    """An OP, kept in the order given: (name, [ARG...])."""

    def __call__(self, parser, namespace, values, option_string=None):
        namespace.ops.append((option_string[2:], values))


def parse(argv):
    p = argparse.ArgumentParser(
        prog="stage.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    p.add_argument("--manifest", nargs=2, metavar=("DIR", "STAMP"))
    for name in ("--out", "--stamp", "--depfile", "--strip", "--readelf", "--glibc-max"):
        p.add_argument(name)
    p.set_defaults(ops=[])
    for name, metavar in (
        ("--tree", ("SRC", "DEST")),
        ("--built-tree", ("SRC", "DEST")),
        ("--file", ("SRC", "DEST", "MODE")),
        ("--link", ("TARGET", "DEST")),
        ("--dir", ("DEST", "MODE")),
    ):
        p.add_argument(name, nargs=len(metavar), metavar=metavar, action=Op)
    opts = p.parse_args(argv)
    if not opts.manifest:
        for k in ("out", "stamp", "depfile"):
            if getattr(opts, k) is None:
                p.error(f"--{k} is required")
    return opts


def is_elf(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    with open(path, "rb") as f:
        return f.read(4) == b"\x7fELF"


def normalise_modes(src, dst):
    """What --tree copied from src to dst: directories 755, files 644 (755 if src's has an x bit);
    symlinks as they are."""
    for dirpath, dirnames, filenames in os.walk(src):
        d = os.path.normpath(os.path.join(dst, os.path.relpath(dirpath, src)))
        os.chmod(d, 0o755)
        for n in filenames:
            p = os.path.join(d, n)
            if n == "meson.build" or os.path.islink(p) or not os.path.lexists(p):
                continue
            os.chmod(p, 0o755 if os.lstat(os.path.join(dirpath, n)).st_mode & 0o111 else 0o644)


def vkey(v):
    return tuple(int(x) for x in v.split("."))


def main():
    opts = parse(sys.argv[1:])
    if opts.manifest:
        # only (re)write STAMP from DIR's contents
        write_if_changed(opts.manifest[1], manifest(opts.manifest[0]) + "\n")
        return
    out = opts.out.rstrip("/")
    tmp = out + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    deps = []

    def dest(rel):
        p = os.path.normpath(os.path.join(tmp, rel))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        return p

    for op, args in opts.ops:
        if op in ("tree", "built-tree"):
            src, rel = args
            ignore = None
            if op == "tree":
                ignore = shutil.ignore_patterns("meson.build")
                deps.append(src)
                for dirpath, dirnames, filenames in os.walk(src):
                    deps += [
                        os.path.join(dirpath, n) for n in dirnames + filenames if n != "meson.build"
                    ]
            shutil.copytree(
                src,
                os.path.normpath(os.path.join(tmp, rel)),
                symlinks=True,
                dirs_exist_ok=True,
                ignore=ignore,
            )
            if op == "tree":
                normalise_modes(src, os.path.normpath(os.path.join(tmp, rel)))
        elif op == "file":
            src, rel, mode = args
            deps.append(src)
            d = dest(rel)
            if os.path.lexists(d):
                os.unlink(d)
            shutil.copyfile(src, d)
            os.chmod(d, int(mode, 8))
        elif op == "link":
            target, rel = args
            d = dest(rel)
            if os.path.lexists(d):
                os.unlink(d)
            os.symlink(target, d)
        elif op == "dir":
            rel, mode = args
            d = os.path.normpath(os.path.join(tmp, rel))
            os.makedirs(d, exist_ok=True)
            os.chmod(d, int(mode, 8))

    elves = []
    for dirpath, dirnames, filenames in os.walk(tmp):
        elves += [os.path.join(dirpath, n) for n in filenames if is_elf(os.path.join(dirpath, n))]
    if opts.strip:
        for p in elves:
            mode = os.stat(p).st_mode
            os.chmod(p, mode | stat.S_IWUSR)
            subprocess.run([opts.strip, "--strip-unneeded", p], check=True)
            os.chmod(p, mode)
    if opts.glibc_max:
        bad = []
        for p in elves:
            r = subprocess.run([opts.readelf, "-V", p], capture_output=True, text=True, check=True)
            vs = re.findall(r"Name: GLIBC_([0-9.]+)", r.stdout)
            if vs:
                v = max(vs, key=vkey)
                if vkey(v) > vkey(opts.glibc_max):
                    bad.append(f"{os.path.relpath(p, tmp)} needs GLIBC_{v}")
        if bad:
            shutil.rmtree(tmp)
            sys.exit(
                "newer than the firmware's glibc %s:\n  %s" % (opts.glibc_max, "\n  ".join(bad))
            )

    old = out + ".old"
    shutil.rmtree(old, ignore_errors=True)
    if os.path.lexists(out):
        os.rename(out, old)
    os.rename(tmp, out)
    shutil.rmtree(old, ignore_errors=True)

    write_if_changed(opts.stamp, manifest(out) + "\n")
    write_depfile(opts.depfile, opts.stamp, deps)


if __name__ == "__main__":
    main()
