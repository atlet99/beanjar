# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# K8LPN retains the standard EGL boundary; its board is firmware-derived.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W23P 13.41.13 and 23.25.55: libGLESv2_PVR_MESA.so prints "OpenGL ES 3.2 build
# 1.15@5986747"; 33.31.61 and 43.22.76 "OpenGL ES 3.2 build 24.1@6525005" (PowerVR BXE-4-32)
gl_es_version = '3.2'
