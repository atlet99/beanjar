# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The initramfs's static arm64 busybox (subprojects/busybox.wrap), configured by
# configs/webos_initramfs.config: the applets guest/initramfs/rootfs/, its scripts and the tests typed on its
# console use, everything else off (docs/dev/guides/build.md, "Initramfs busybox"). Built by its own Kbuild
# with CROSS_COMPILE from the arm64_cross_compile option (meson/kbuild.sh).
# It depends on nothing of the target: it is built once, in <shared_dir>/busybox/<key> (meson/shared.py;
# the key: the compiler prefix and the source tree), and each target's build copies it; a changed
# input (the tree's wrap key, the fragment, the script, the compiler's version) runs make there again.
busybox_config = files('configs/webos_initramfs.config')
busybox_root = shared_dir / 'busybox' / run_command(python, shared_py, 'key',
  'cross_compile ' + get_option('arm64_cross_compile'), 'source ' + sources['busybox']['dir'],
  check: true).stdout().strip()
busybox = custom_target('busybox',
  output: 'busybox',
  depfile: 'busybox.d',
  depends: sources['busybox']['stamp'],
  depend_files: [kbuild, busybox_config],
  command: [python, shared_py, 'run', '--tool', get_option('arm64_cross_compile') + 'gcc',
            '--dir', busybox_root, '--depfile', '@DEPFILE@',
            '--input', sources['busybox']['dir'] / '.wrap-key', kbuild, busybox_config,
            '--file', busybox_root / 'busybox', '@OUTPUT@', '--',
            kbuild, 'busybox', sources['busybox']['dir'], busybox_root / 'obj',
            get_option('arm64_cross_compile'), busybox_config, busybox_root / 'busybox'],
  console: true)
# The x86-64 guest kernel's initramfs (guest/kernel/meson.build, x86_64_kernel): the same busybox,
# static x86-64, by the build machine's own compiler.
if x86_64_kernel
  busybox_x86_64_root = shared_dir / 'busybox' / run_command(python, shared_py, 'key',
    'arch x86_64', 'cross_compile ', 'source ' + sources['busybox']['dir'],
    check: true).stdout().strip()
  busybox_x86_64 = custom_target('busybox-x86_64',
    output: 'busybox-x86_64',
    depfile: 'busybox-x86_64.d',
    depends: sources['busybox']['stamp'],
    depend_files: [kbuild, busybox_config],
    env: {'GUEST_ARCH': 'x86_64'},
    command: [python, shared_py, 'run', '--tool', 'gcc',
              '--dir', busybox_x86_64_root, '--depfile', '@DEPFILE@',
              '--input', sources['busybox']['dir'] / '.wrap-key', kbuild, busybox_config,
              '--file', busybox_x86_64_root / 'busybox', '@OUTPUT@', '--',
              kbuild, 'busybox', sources['busybox']['dir'], busybox_x86_64_root / 'obj',
              '', busybox_config, busybox_x86_64_root / 'busybox'],
    console: true)
endif
