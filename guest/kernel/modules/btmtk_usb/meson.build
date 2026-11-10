# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# btmtk_usb.ko, MediaTek's Bluetooth USB driver as webos-bluetooth-service sees it with no adapter
# (its netlink family and /sys/module/btmtk_usb): built as lgemu.ko is (docs/dev/guides/build.md, "Kernel
# module lgemu"), carried in the initramfs, and loaded by /init when boot.conf's SOC_MODULES names
# it (the chip table, guest/targets/soc/: m3, k5lp). Published as build/btmtk_usb.ko
# too.
btmtk_usb_ko = custom_target('btmtk_usb',
  output: 'btmtk_usb.ko',
  depends: [sources['linux']['stamp'], kernel_outputs],
  depend_files: [kbuild, files('Kbuild', 'btmtk_usb.c')],
  command: [kbuild, 'module', sources['linux']['dir'], kernel_obj,
            get_option('arm64_cross_compile'), meson.current_source_dir(),
            meson.current_build_dir() / 'obj', '@OUTPUT@'],
  console: true,
  build_by_default: true)
published += [['file', btmtk_usb_ko.full_path(), 'btmtk_usb.ko']]
publish_depends += btmtk_usb_ko
