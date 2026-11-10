# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The viewer's window (beanjar_ui/viewer): its layouts (*.blp here) and icons (*.svg) as one
# GResource bundle, published as build/viewer/beanjar-viewer.gresource, next to beanjar_ui; only
# with GTK 4 for Python (meson/meson.build).
if have_gtk
  viewer_ui = custom_target('beanjar-viewer-ui',
    input: files('window.blp', 'pane.blp', 'statusbar.blp', 'statusfield.blp',
                 'remote-window.blp'),
    output: ['window.ui', 'pane.ui', 'statusbar.ui', 'statusfield.ui', 'remote-window.ui'],
    command: [blueprint, 'batch-compile', '@OUTDIR@', '@CURRENT_SOURCE_DIR@', '@INPUT@'])
  viewer_gresource = gnome.compile_resources('beanjar-viewer', 'beanjar-viewer.gresource.xml',
    source_dir: [meson.current_build_dir(), meson.current_source_dir(), top / 'host' / 'ui' / 'data'],
    dependencies: viewer_ui,
    gresource_bundle: true)
  published += [['file', viewer_gresource.full_path(), 'viewer/beanjar-viewer.gresource']]
  publish_depends += viewer_gresource
endif
