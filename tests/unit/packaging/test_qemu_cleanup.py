#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Where QEMU is not built, the build removes a leftover published qemu tree, and only there.

A full configure of the host build is too heavy for a test, so this runs the removal script directly
and checks that host/tools/build/qemu/meson.build wires it only into the branch where the option
disables the QEMU build.
"""

import os, re, subprocess, sys, tempfile

import pytest


def test_qemu_cleanup():
    top = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    qdir = os.path.join(top, "host/tools/build/qemu")
    script = os.path.join(qdir, "remove-published.py")

    with tempfile.TemporaryDirectory() as tmp:
        tree = os.path.join(tmp, "qemu")
        os.makedirs(os.path.join(tree, "bin"))
        open(os.path.join(tree, "bin/qemu-system-aarch64"), "w").close()
        sibling = os.path.join(tmp, "qemu-other")
        os.makedirs(sibling)
        stamp = os.path.join(tmp, "stamp")
        subprocess.run([script, tree, stamp], check=True, stdout=subprocess.DEVNULL)
        assert not os.path.exists(tree), "tree not removed"
        assert os.path.isdir(sibling), "sibling removed"
        assert os.path.exists(stamp)
        subprocess.run([script, tree, stamp], check=True, stdout=subprocess.DEVNULL)  # absent: fine
        other = os.path.join(tmp, "keep")
        os.makedirs(other)
        r = subprocess.run([script, other, stamp], stderr=subprocess.DEVNULL)
        assert r.returncode != 0 and os.path.isdir(other), "removed a directory not named qemu"

    src = open(os.path.join(qdir, "meson.build")).read()
    build_if = "if not qemu_opt.disabled() and qemu_missing.length() == 0\n"
    assert src.count(build_if) == 1, "no QEMU build branch"
    before, branch = src.split(build_if)
    assert "remove-published" not in before
    enabled, disabled = re.split(r"^else\n", branch, maxsplit=1, flags=re.M)
    assert "remove-published" not in enabled, "cleanup wired in the enabled branch"
    assert disabled.count("find_program('remove-published.py')") == 1
    tail = disabled[disabled.index("custom_target('qemu-remove-published'") :]
    assert "build_by_default: true" in tail and "build_always_stale: true" in tail
