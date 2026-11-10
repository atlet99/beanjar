# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libhal_crypto'] (libhal_crypto.so.0 on H13, .so.2 on M14, .so on M2): replaces
# the stock symlink, with that SONAME, and NEEDs the stock file (unchanged). The one TrustZone secure-store item the
# userland needs without LG's security engine, the model secret that libsecretagent turns into the
# NDUID (hal-crypto.c explains the chain). HAL_CRYPTO_ReadMskToSecureStore resolves here and every
# other symbol in LG's library.
hal_crypto_stock = shared_library('libhal_crypto', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libhal_crypto'][1].substring(14))
hal_crypto_soname = hal_stock['libhal_crypto'][0].split('.so.')
hal_crypto_kwargs = hal_crypto_soname.length() > 1 ? {'soversion': hal_crypto_soname[1]} : {}
hal_crypto = shared_library('hal_crypto', 'hal-crypto.c',
  kwargs: hal_crypto_kwargs,
  link_args: ['-Wl,--no-as-needed', hal_crypto_stock.full_path()],
  link_depends: hal_crypto_stock)
hal_libs += {hal_stock['libhal_crypto'][0]: hal_crypto}
hal_libs_stock += {hal_stock['libhal_crypto'][0]: hal_stock['libhal_crypto'][1]}
