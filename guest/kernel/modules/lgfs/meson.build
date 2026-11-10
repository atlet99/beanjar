# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# lgfs.ko, LG's filesystem names (tfat, tntfs, ntfs) on the kernel's vfat and ntfs3 (lgfs.c): built as
# lgemu.ko is (docs/dev/guides/build.md, "Kernel module lgemu"), but loaded with the kernel's own
# peripheral modules: the kernel target's modules tree with it in extra/ and depmod run again
# (guest/kernel/meson.build, peripheral_modules), so /init's modprobe finds it when boot.conf's
# PERIPHERAL_MODULES names it. Published as build/lgfs.ko too.
lgfs_ko = custom_target('lgfs',
  output: 'lgfs.ko',
  depends: [sources['linux']['stamp'], kernel_outputs],
  depend_files: [kbuild, files('Kbuild', 'lgfs.c', 'lgfs-options.h')],
  command: [kbuild, 'module', sources['linux']['dir'], kernel_obj,
            get_option('arm64_cross_compile'), meson.current_source_dir(),
            meson.current_build_dir() / 'obj', '@OUTPUT@'],
  console: true,
  build_by_default: true)
published += [['file', lgfs_ko.full_path(), 'lgfs.ko']]
publish_depends += lgfs_ko

