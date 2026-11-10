# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libdile_vt'] (libdile_vt.so.0, DILE_VT's older interface: H13
# only, guest/layers/soc/<chip>/meson.build): replaces the stock symlink, with that SONAME, and NEEDs the
# stock file (unchanged): the video plane for tv.capture (dile-vt.c),
# in a process that also has the libhal_lg115x shim and the emulator's physical memory
# (dile-common.h); LG's DILE_VT everywhere else.
dile_vt_stock = shared_library('libdile_vt', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libdile_vt'][1].substring(11))
dile_vt = shared_library('dile_vt', 'dile-vt.c',
  soversion: hal_stock['libdile_vt'][0].split('.so.')[1],
  link_args: ['-Wl,--no-as-needed', dile_vt_stock.full_path(), '-ldl', '-lpthread'],
  link_depends: dile_vt_stock)
hal_libs += {hal_stock['libdile_vt'][0]: dile_vt}
hal_libs_stock += {hal_stock['libdile_vt'][0]: hal_stock['libdile_vt'][1]}
