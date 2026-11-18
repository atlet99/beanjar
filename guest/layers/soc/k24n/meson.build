# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# K24N keeps K24's boundaries (Mali, libmali.so.0 with gbm: W25H's r51 exports libgbm's API, and
# bsppart adds a libgbm.so.1 wrapper NEEDing it); its boards are its own firmware's.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W25H 33.31.68 and 43.22.66: libmali.so.0.51.0 prints "OpenGL ES 3.2"
# (mali_kbase.ko 43.22.66: "core:Mali-G57")
gl_es_version = '3.2'
