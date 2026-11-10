# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# NYX's HAL entry point uses the same shared software engine as DILE callers.
halgal_stock = shared_library('libhalgal', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libhalgal'][1].substring(10))
halgal = shared_library('halgal', 'halgal.c',
  c_args: ['-fvisibility=hidden'],
  link_with: dile_gal,
  link_args: ['-Wl,--no-as-needed', halgal_stock.full_path()],
  link_depends: halgal_stock)
hal_libs += {hal_stock['libhalgal'][0]: halgal}
hal_libs_stock += {hal_stock['libhalgal'][0]: hal_stock['libhalgal'][1]}
