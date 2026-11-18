# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# O24 keeps O22N2's boundaries; its boards are its own firmware's.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# Its media elements are the release's (o22).
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W24O 23.23.30, 33.31.68 and 43.21.71: libmali.so.0.46.0 prints "OpenGL ES 3.2"
gl_es_version = '3.2'
