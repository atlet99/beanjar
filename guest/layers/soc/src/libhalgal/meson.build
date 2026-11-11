# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# NYX's HAL entry point uses the same software engine as DILE callers: the libdile_gal shim's where
# the firmware has a stock libdile_gal, else its own copy of the engine (halgal_own_engine,
# guest/layers/soc/meson.build), which exports only HAL_GAL_* (halgal.map).
halgal_stock = shared_library('libhalgal', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libhalgal'][1].substring(10))
if halgal_own_engine
  gal_engine = '../libdile_gal'
  halgal = shared_library('halgal', 'halgal.c',
    gal_engine / 'dile-gal.c', gal_engine / 'gfx-memory.c', gal_engine / 'gfx-merge.c',
    gal_engine / 'gal-draw.c', gal_engine / 'gal-image.c', gal_engine / 'cursor-channel.c',
    gal_engine / 'osd-readback.c',
    c_args: ['-fvisibility=hidden',
      '-DGAL_CAPABILITY_BYTES=@0@'.format(get_variable('dile_gal_capability_bytes', 72))],
    link_args: ['-Wl,--version-script=' + meson.current_source_dir() / 'halgal.map',
      '-Wl,--no-as-needed', halgal_stock.full_path(), '-ldl', '-lpthread'],
    link_depends: [halgal_stock, files('halgal.map')])
else
  halgal = shared_library('halgal', 'halgal.c',
    c_args: ['-fvisibility=hidden'],
    link_with: dile_gal,
    link_args: ['-Wl,--no-as-needed', halgal_stock.full_path()],
    link_depends: halgal_stock)
endif
hal_libs += {hal_stock['libhalgal'][0]: halgal}
hal_libs_stock += {hal_stock['libhalgal'][0]: hal_stock['libhalgal'][1]}
