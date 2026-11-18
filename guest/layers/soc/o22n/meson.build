# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# O22N keeps the initial LG GAL boundary; board options are firmware-derived.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# W23O number1's AAC drc-mode default differs from W22O's; from ombre on, the release's (o22)
if 'ombre' not in webos_reached
  gst_elements = 'lx-o22n'
endif
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W23O 23.25.55, 33.31.68 and 44.00.92: libmali.so.0.46.0 prints "OpenGL ES 3.2"
gl_es_version = '3.2'
