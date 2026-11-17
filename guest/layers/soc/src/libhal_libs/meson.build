# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libhal_libs'] (MStar LM21's board HAL, bsppart's libhal_libs.so.0.1.0, SONAME
# libhal_libs.so): replaces the stock symlink, with that SONAME, and NEEDs the stock file (unchanged). Only
# HAL_PVR_CRYPTO_GetSecureKey is the emulator's, the DVR's key from the VM's store (hal-pvrkey.c); every
# other symbol is LG's.
hal_libs_stock_lib = shared_library('libhal_libs', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libhal_libs'][1].substring(12))
hal_libs_lib = shared_library('hal_libs', 'hal-pvrkey.c',
  link_args: ['-Wl,--no-as-needed', hal_libs_stock_lib.full_path()],
  link_depends: hal_libs_stock_lib)
hal_libs += {hal_stock['libhal_libs'][0]: hal_libs_lib}
hal_libs_stock += {hal_stock['libhal_libs'][0]: hal_stock['libhal_libs'][1]}
