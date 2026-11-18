# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
if 'spacebuns' in webos_reached
  # The legacy media path does not apply to Weston13 (spacebuns, and the releases after it).
  gst_plugin_dir = ''
endif
# The media elements are the release's (o22). Clear H.264/HEVC/AAC transport. The AAC-only plugin
# intentionally omits other stock codec factories; this is not complete default media qualification.
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W24G 33.31.68 and 43.21.73: libmali.so.0.46.0 prints "OpenGL ES 3.2"
gl_es_version = '3.2'
