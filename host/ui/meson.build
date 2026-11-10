# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The desktop windows (README.md, docs/dev/architecture/viewer.md): the beanjar_ui package, GTK 4 and
# libadwaita over the core package beanjar (host/core). Its layouts are built into two GResource
# bundles, the VM manager's (data/manager) and the viewer's (data/viewer). The package and the
# viewer's launcher (bin/beanjar-viewer) are published to build/viewer/, beside the viewer's core
# (host/viewer), which the launcher runs headless.
subdir('data/manager')
subdir('data/viewer')
published += [
  ['file', meson.current_source_dir() / 'bin/beanjar-viewer', 'viewer/beanjar-viewer'],
  ['tree', meson.current_source_dir() / 'beanjar_ui', 'viewer/beanjar_ui'],
]
