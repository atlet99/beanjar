# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# W22P K8LP: initial K7LP Realtek HAL and media boundary; runtime validation pending.
# PowerVR users link the standard EGL/GLES ABI, so Mesa supplies those libraries
# directly. Do not inherit K7LP's libmali vendor stand-in.
gl_vendor_stubs = []
gl_vendor_gl = ''
gl_vendor_gl_soname = ''
gl_standard_abi = true
# libhalgal: every K8 firmware's captureservice calls HAL_GAL_* itself (bsppart's libhalgal.so.1,
# not DILE_GAL), and the stock HAL_GAL_Init's KADP_GAL_Init reaches RHAL_SE_Init's se_open, which
# asserts (se_drv.c:167) when /dev/se0 cannot be mapped: captureservice aborted at every start
# on number1. The shim serves HAL_GAL from the DILE_GAL software engine, as on queue and O22N2.
hal_stock += {'libhalgal': ['libhalgal.so', 'libhalgal.so.1.0.0', ['libhalgal.so.1'], 'bsppart']}
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
