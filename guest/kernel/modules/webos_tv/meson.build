# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# webos_tv.ko, LG's DVBv5 tuner driver as bsppart's libdvbfrontend and legacy-broadcast's libdvbv5
# see it (the wdvb/wfm sysfs, the DVB adapter and its frontends on the built-in DVB core):
# built as lgemu.ko is (docs/dev/guides/build.md, "Kernel module lgemu"), carried in the initramfs,
# and loaded by /init when boot.conf's SOC_MODULES names it (the chip table,
# guest/targets/soc/). Published as build/webos_tv.ko too. Its exports
# (webos_tv.h) are dtv_soc.ko's.
webos_tv_dir = kernel_root / 'webos_tv'
webos_tv_inputs = kbuild + [files('Kbuild', 'webos_tv.c', 'webos_tv.h'), lgemu_h]
webos_tv_ko = custom_target('webos_tv',
  output: 'webos_tv.ko',
  depfile: 'webos_tv.ko.d',
  depends: [sources['linux']['stamp'], kernel_outputs, lgemu_ko],
  depend_files: webos_tv_inputs,
  command: kernel_shared + ['--dir', webos_tv_dir, '--after', kernel_root, '--after', lgemu_dir, '--depfile', '@DEPFILE@',
            '--input', webos_tv_inputs, '--file', webos_tv_dir / 'webos_tv.ko', '@OUTPUT@', '--',
            kbuild, 'module', sources['linux']['dir'], kernel_obj, get_option('arm64_cross_compile'),
            meson.current_source_dir(), webos_tv_dir / 'obj', webos_tv_dir / 'webos_tv.ko', lgemu_symvers],
  console: true,
  build_by_default: true)
webos_tv_h = files('webos_tv.h')
webos_tv_symvers = webos_tv_dir / 'obj' / 'Module.symvers'
published += [['file', webos_tv_ko.full_path(), 'webos_tv.ko']]
publish_depends += webos_tv_ko

