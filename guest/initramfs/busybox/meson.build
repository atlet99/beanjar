# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The initramfs's static arm64 busybox (subprojects/busybox.wrap), configured by
# configs/webos_initramfs.config: the applets guest/initramfs/rootfs/, its scripts and the tests typed on its
# console use, everything else off (docs/dev/guides/build.md, "Initramfs busybox"). Built by its own Kbuild
# with CROSS_COMPILE from the arm64_cross_compile option (meson/kbuild.sh).
busybox_config = files('configs/webos_initramfs.config')
busybox = custom_target('busybox',
  output: 'busybox',
  depends: sources['busybox']['stamp'],
  depend_files: [kbuild, busybox_config],
  command: [kbuild, 'busybox', sources['busybox']['dir'], meson.current_build_dir() / 'obj',
            get_option('arm64_cross_compile'), busybox_config, '@OUTPUT@'],
  console: true)
