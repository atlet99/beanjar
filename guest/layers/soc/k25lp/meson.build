# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# K25LP keeps K8LPN2's boundaries (PowerVR, standard EGL); its boards are its own firmware's.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W25P 33.31.61: libGLESv2_PVR_MESA.so prints "OpenGL ES 3.2 build 24.1@6525005";
# 43.22.74 "OpenGL ES 3.2 build 25.1@6715691" (PowerVR C-Series CXM-2-64)
gl_es_version = '3.2'
