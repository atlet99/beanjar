# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Replace nn-proxy's AI BSP soname, NEEDing the unchanged stock file for every other symbol.
aibsp_stock = shared_library('libaibsp', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libaibsp'][1].substring(9))
aibsp = shared_library('aibsp', 'aibsp.c',
  soversion: hal_stock['libaibsp'][0].split('.so.')[1],
  link_args: ['-Wl,--no-as-needed', aibsp_stock.full_path()],
  link_depends: aibsp_stock)
hal_libs += {hal_stock['libaibsp'][0]: aibsp}
hal_libs_stock += {hal_stock['libaibsp'][0]: hal_stock['libaibsp'][1]}
