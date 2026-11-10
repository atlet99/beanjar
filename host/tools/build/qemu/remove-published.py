#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Remove a published QEMU tree (usage: remove-published.py <publish_dir>/qemu <stamp>).

Only used where the build does not build QEMU: publish.py never removes a tree, and the launcher
prefers a published one to the system QEMU. Removes exactly that directory, and only one named qemu.
"""
import os, shutil, sys

tree, stamp = sys.argv[1:3]
if os.path.basename(os.path.normpath(tree)) != 'qemu':
    sys.exit('refusing to remove %s: not a qemu tree' % tree)
if os.path.islink(tree):
    os.unlink(tree)
    print('removed the leftover', tree)
elif os.path.isdir(tree):
    shutil.rmtree(tree)
    print('removed the leftover', tree)
open(stamp, 'w').close()
