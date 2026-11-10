# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Shared tools for running the firmware's own EEPROM code. The preload variant
# lets libc load a PIE storage executable normally before entering the probe.
lls_dl = meson.get_compiler('c').find_library('dl')
lls_probe = executable('beanjar-lls-probe', 'lls-probe.c', dependencies: lls_dl)
lls_preload = shared_library('beanjar-lls-preload', 'lls-probe.c',
  c_args: ['-DLLS_PRELOAD'], dependencies: lls_dl, name_prefix: '')
published += [
  ['file', lls_probe.full_path(), 'lls/lls-probe'],
  ['file', lls_preload.full_path(), 'lls/lls-probe.so'],
]
publish_depends += [lls_probe, lls_preload]
