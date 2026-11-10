# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libhal_avd'] (MStar's board HAL, bsppart's: libhal_msd93f2g.so on
# M2, libhal_msd94bcg.so on M3): replaces the stock symlink, with that SONAME, and NEEDs the stock
# file (unchanged). Loaded by tvservice. Only the analog decoder's two answers are the emulator's,
# the LG115x shim's (libhal_lg115x/hal-avd.c, HAL_AVD_OVER_STOCK): HAL_VFE_AVD_DoesSyncExist and
# HAL_VFE_AVD_GetTimingInfo of a decoder with no signal; Connect is noted on its way to the stock
# one, and everything else is MStar's over mstar-utopia.ko. The stock GetTimingInfo, with the
# decoder unlocked on the tuner's source, makes up a free-running timing with an active size,
# which tvservice's AVD task counts as a good signal.
hal_avd_base = hal_stock['libhal_avd'][0].split('.so')[0]
hal_avd_stock = shared_library(hal_avd_base, hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libhal_avd'][1].replace(hal_avd_base + '.', ''))
hal_avd = shared_library(hal_avd_base.substring(3), '../libhal_lg115x/hal-avd.c',
  c_args: ['-DHAL_AVD_OVER_STOCK', '-D_GNU_SOURCE'],
  link_with: hal_common,
  link_args: ['-Wl,--no-as-needed', hal_avd_stock.full_path(), '-ldl'],
  link_depends: hal_avd_stock)
hal_libs += {hal_stock['libhal_avd'][0]: hal_avd}
hal_libs_stock += {hal_stock['libhal_avd'][0]: hal_stock['libhal_avd'][1]}
