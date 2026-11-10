# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# media-pool.ko, the media pool's device (main.c; include/webos-media.h, "The pool"), over the
# kernel's webos-ivshmem (patch 0018): built as lgemu.ko is (docs/dev/guides/build.md, "Kernel module
# lgemu"), loaded by guest/initramfs/rootfs/init after lgemu.ko. Published as build/media-pool.ko
# too, for a manual rmmod/insmod from /host.
media_pool_ko = custom_target('media-pool',
  output: 'media-pool.ko',
  depends: [sources['linux']['stamp'], kernel_outputs],
  depend_files: [kbuild, files('Kbuild', 'main.c', 'grants.c', 'grants.h', 'tests/media-pool-kunit.c',
                              '../../../../include/webos-media.h')],
  command: [kbuild, 'module', sources['linux']['dir'], kernel_obj,
            get_option('arm64_cross_compile'), meson.current_source_dir(),
            meson.current_build_dir() / 'obj', '@OUTPUT@'],
  console: true,
  build_by_default: true)
published += [['file', media_pool_ko.full_path(), 'media-pool.ko']]
publish_depends += media_pool_ko

# The device's ABI as a webOS process sees it (32-bit, through compat_ioctl): a kselftest-style
# TAP test run in the guest against the host's media helper (tests/media-pool-selftest.c), and
# tests/run-tests.py, which runs it, the module load loops and the doorbell across a pause, in a
# running VM:  meson compile -C <guest build> media-pool-tests  with VM=[HOST/]NAME in the
# environment (KUNIT=1 too for the KUnit module of a debug kernel build).
media_pool_selftest = executable('media-pool-selftest', 'tests/media-pool-selftest.c',
  link_args: ['-static', '-s'], install: false, build_by_default: true)
run_target('media-pool-tests',
  command: [python, files('tests/run-tests.py'), '--selftest', media_pool_selftest, '--module', media_pool_ko,
            '--kunit-dir', meson.current_build_dir() / 'obj'],
  depends: [media_pool_selftest, media_pool_ko])
