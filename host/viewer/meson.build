# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The desktop viewer (README.md, docs/dev/architecture/viewer.md), published to build/viewer/. It runs
# on the QEMU host: the x86_64 workstation (WSLg) and the aarch64 Pi.
#
#   libbeanjar-viewer-core.so  the frame path in C (core.h): D-Bus display listener, scene, cursor,
#                            video windows, media decoding, audio, input, the remote's IR
#   viewer-headless    the core without a desktop window (SDL offscreen): tests, scripts
#
# The launcher beanjar-viewer and its GTK 4 window (beanjar_ui.viewer) are host/ui's, published
# beside these; for --headless/--replay/--screenshot the launcher runs the program above.
fs = import('fs')
sdl2_dep = dependency('sdl2', version: '>= 2.26')
glesv2_dep = dependency('glesv2')
# The core's files live in sources.txt (one space-separated line, relative to
# this directory) so Meson and ad-hoc trial builds compile the same list.
core_sources = []
foreach s : fs.read('sources.txt').split(' ')
  st = s.strip()
  if st != ''
    core_sources += st
  endif
endforeach
viewer_core = shared_library('beanjar-viewer-core',
  core_sources,
  dependencies: [sdl2_dep, dependency('gio-unix-2.0'), dependency('egl'), glesv2_dep,
                 dependency('threads'), meson.get_compiler('c').find_library('m')],
  gnu_symbol_visibility: 'default',
  override_options: ['c_std=c11', 'debug=true'],
  build_by_default: true)
viewer_headless = executable('viewer-headless', 'core/headless.c',
  link_with: viewer_core,
  dependencies: [sdl2_dep, dependency('glib-2.0')],
  build_rpath: '$ORIGIN:/usr/lib/beanjar/viewer',
  override_options: ['c_std=c11', 'debug=true'],
  build_by_default: true)
published += [
  ['file', viewer_core.full_path(), 'viewer/libbeanjar-viewer-core.so'],
  ['file', viewer_headless.full_path(), 'viewer/viewer-headless'],
]
publish_depends += [viewer_core, viewer_headless]
