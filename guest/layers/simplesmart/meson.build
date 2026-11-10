# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The simplesmart layer, build/layers/m2-simplesmart/simplesmart: SimpleSmart SS1A's one
# always-used layer (docs/dev/architecture/simplesmart.md), stacked over its stock root (root.pak):
#   rootfs/etc/emulator/board.cmdline   the bootloader's parameters (targets.py's Soc.board_src)
#   rootfs/sbin/insmod, rmmod, modprobe the firmware's module tools: a logging no-op (its modules are ARMv7)
#   lib/libhalgal.so                    the graphics HAL on the emulator's GOP (src/libhalgal), in
#                                       /lib, which rc.local's LD_LIBRARY_PATH has before
#                                       /mnt/vendor (the stock one's place)
halgal = shared_library('halgal', 'src/libhalgal/halgal.c', 'src/libhalgal/decode.c',
  '../soc/src/libdile_gal/gal-draw.c',
  include_directories: include_directories('../soc/src/libdile_gal'),
  c_args: ['-fvisibility=hidden'],
  link_args: ['-Wl,-soname,libhalgal.so', '-lpthread', '-ldl'])
simplesmart_args = ['--tree', meson.current_source_dir() / 'rootfs', '.',
  '--file', halgal, 'lib/libhalgal.so', '755']
layers += {'simplesmart': custom_target('simplesmart-layer',
  output: 'simplesmart-layer.stamp',
  depfile: 'simplesmart-layer.d',
  command: arm_stage + ['--out', layers_dir / 'simplesmart', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@']
    + simplesmart_args,
  build_by_default: true)}
