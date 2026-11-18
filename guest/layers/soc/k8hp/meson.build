# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Initial W22 K8HP profile: K8LP Realtek HAL/media and standard EGL boundary.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W22H 04.60.85, 13.30.61 and 23.25.75: libGLESv2_PVR_MESA.so prints "OpenGL ES 3.2
# build 1.15@5986747"; 43.22.40 "OpenGL ES 3.2 build 24.1@6525005" (PowerVR BXE-4-32)
gl_es_version = '3.2'
