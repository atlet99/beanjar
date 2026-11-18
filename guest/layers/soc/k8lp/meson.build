# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# W22P K8LP: initial K7LP Realtek HAL and media boundary; runtime validation pending.
# PowerVR users link the standard EGL/GLES ABI, so Mesa supplies those libraries
# directly. Do not inherit K7LP's libmali vendor stand-in.
gl_vendor_stubs = []
gl_vendor_gl = ''
gl_vendor_gl_soname = ''
gl_standard_abi = true
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: W22P 04.60.90: libGLESv2_PVR_MESA.so prints "OpenGL ES 3.2 build 1.15@5986747"
# (PowerVR Rogue BXE-4-32)
gl_es_version = '3.2'
