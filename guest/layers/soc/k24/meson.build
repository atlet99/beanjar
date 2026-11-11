# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# K24 keeps K8HPP's Realtek HAL and media boundaries (the same KADP, libhalgal and stock media
# plugins); its boards are its own firmware's. Its GPU is Mali, not the K8 chips' PowerVR: bsppart's
# libmali.so.0.41.0 (ombre) or .0.51.0 (ponytail, queue), SONAME libmali.so.0, with 5 KB
# libEGL.so.1, libGLESv2.so.2 and libGLESv1_CM.so.1 wrappers NEEDing it. The firmware's GL users
# (Qt5Gui, the compositor's QPAs, libim, libcbe, libgm-wayland, ...) NEED libmali.so.0 itself; on
# queue they take gbm_* from it too (its r51 exports libgbm's API; nothing NEEDs libgbm.so.1; the
# ombre and ponytail builds export none and nothing imports it), and wl_egl_window_* from libwayland-egl.so.1, which some
# NEED. So the gl layer's vendor stand-in is O22's libmali.so.0 with Mesa's libgbm added; the stock
# gpu-libs unit (mali_kbase) stays K7LP's no-op.
gl_vendor_stubs = []
gl_vendor_gl = 'mali'
gl_vendor_gl_soname = 'libmali.so.0'
gl_vendor_gl_needs = ['EGL', 'GLESv2', 'GLESv1_CM', 'wayland-egl', 'gbm']
gl_standard_abi = false
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
