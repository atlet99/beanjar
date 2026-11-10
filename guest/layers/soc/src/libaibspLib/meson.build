# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Replace pqcontroller's AI picture soname, NEEDing the unchanged stock file for every other symbol.
aibsp_lib_stock = shared_library('libaibspLib', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libaibspLib'][1].substring(12))
aibsp_lib = shared_library('aibspLib', 'aibsp-lib.c',
  soversion: hal_stock['libaibspLib'][0].split('.so.')[1],
  link_args: ['-Wl,--no-as-needed', aibsp_lib_stock.full_path()],
  link_depends: aibsp_lib_stock)
hal_libs += {hal_stock['libaibspLib'][0]: aibsp_lib}
hal_libs_stock += {hal_stock['libaibspLib'][0]: hal_stock['libaibspLib'][1]}
