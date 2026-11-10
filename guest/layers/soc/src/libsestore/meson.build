# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libsestore'] (libsestore.so.0): replaces the stock symlink and NEEDs the
# stock file (unchanged). SESTORE_ReadData is a client of the provisioned kernel store on every
# target with this shim. With sestore_model_items (O22's firmwares
# whose nyx secure store reads libsestore: queue, spacebuns), fallback regional-model requests
# retain their plain-model alias. Other symbols, and missing items with fallback policy,
# resolve to LG's library.
sestore_stock = shared_library('libsestore', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libsestore'][1].substring(11))
sestore = shared_library('sestore', 'sestore.c',
  include_directories: include_directories('../../../../../include'),
  c_args: get_variable('sestore_model_items', false) ? ['-DSESTORE_MODEL_ITEMS'] : [],
  soversion: hal_stock['libsestore'][0].split('.so.')[1],
  link_args: ['-Wl,--no-as-needed', sestore_stock.full_path(), '-ldl'],
  link_depends: sestore_stock)
hal_libs += {hal_stock['libsestore'][0]: sestore}
hal_libs_stock += {hal_stock['libsestore'][0]: hal_stock['libsestore'][1]}
