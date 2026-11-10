# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libhal_lg115x'] (the chip's table, guest/layers/soc/<chip>/meson.build:
# libhal_lg115x.so.0 on H13, .so.2 on M14): replaces the stock symlink, with that SONAME, and NEEDs
# the stock file (unchanged). Loaded by tvservice and tvwin (M14: securitymanager too). It answers
# the video scaler and panel mute (hal-video.c: the webos.video channel to the viewer), the analog decoder's no-signal
# (hal-avd.c) and the section decoder's failure without its driver (hal-sdec.c).
# HAL_AUDIO_* are the stock ones
# (the master volume and the speaker mute reach lg115x.ko's audio master, which passes them to
# lgemu's playback gain). HAL_I2C_* are the stock ones,
# over the kernel's I2C bus (lg115x.ko, with lgemu.ko's micom, NVM EEPROM and amplifier).
hal_lg115x_stock = shared_library('libhal_lg115x', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libhal_lg115x'][1].substring(14))
hal_lg115x = shared_library('hal_lg115x', 'hal-video.c', 'hal-avd.c', 'hal-sdec.c',
  soversion: hal_stock['libhal_lg115x'][0].split('.so.')[1],
  link_with: hal_common,
  link_args: ['-Wl,--no-as-needed', hal_lg115x_stock.full_path(), '-ldl', '-lpthread', '-lm'],
  link_depends: hal_lg115x_stock)
hal_libs += {hal_stock['libhal_lg115x'][0]: hal_lg115x}
hal_libs_stock += {hal_stock['libhal_lg115x'][0]: hal_stock['libhal_lg115x'][1]}
