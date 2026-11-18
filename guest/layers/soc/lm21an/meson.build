# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# W22A LM21AN keeps the MStar Mali/DILE integration; board options are its own.
# W22A's GStreamer 1.18 and its own element interfaces (no masink).
gst_elements = 'mstar-lm21an'
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W22A 04.60.90: libmali.so.0.1 prints "OpenGL ES 3.2 v1.r23p0-01rel0.Bifrost-20240617.5cf8bc6"
gl_es_version = '3.2'
