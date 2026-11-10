# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The VM manager's window (beanjar_ui/manager): its layouts (*.blp here) are built, with the
# application icon (host/ui/data/icon.svg), into one GResource bundle, published as
# build/manager/beanjar.gresource, where beanjar_ui/manager/ui.py looks for it. Only with GTK 4
# for Python (meson/meson.build).
if have_gtk
  manager_ui = custom_target('beanjar-ui',
    input: files('window.blp', 'confirm.blp', 'vm-form.blp', 'layer-row.blp', 'vm-row.blp',
                 'progress.blp'),
    output: ['window.ui', 'confirm.ui', 'vm-form.ui', 'layer-row.ui', 'vm-row.ui', 'progress.ui'],
    command: [blueprint, 'batch-compile', '@OUTDIR@', '@CURRENT_SOURCE_DIR@', '@INPUT@'])
  manager_gresource = gnome.compile_resources('beanjar', 'beanjar.gresource.xml',
    source_dir: [meson.current_build_dir(), top / 'host' / 'ui' / 'data'],
    dependencies: manager_ui,
    gresource_bundle: true)
  published += [['file', manager_gresource.full_path(), 'manager/beanjar.gresource']]
  publish_depends += manager_gresource
endif
