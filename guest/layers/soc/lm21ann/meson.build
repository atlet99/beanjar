# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# LM21ANN keeps the existing MStar/Mali boundary and its own board options.
# W23A's GStreamer 1.18 and its own element interfaces (no masink).
gst_elements = 'mstar-lm21ann'
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W23A 23.25.60, 33.31.66 and 43.22.x: libmali.so.0.1 prints "OpenGL ES 3.2"
# (mali_kbase.ko 33.31.66: "core:Mali-G52")
gl_es_version = '3.2'
