# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# K6HP inherits the Realtek K6 HAL boundary; this board supplies its own command line.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W20H 04.74.00: libmali.so.1.0 prints "OpenGL ES 3.2 v1.r11p0-01rel0" (the DDK's
# level; the fitted part is not named)
gl_es_version = '3.2'
