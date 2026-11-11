# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# dtv_soc.ko, the SoC's demodulator output and demux, fed from the host (include/webos-tuner.h):
# built as webos_tv.ko is, against its exports, carried in the initramfs and loaded after it when
# boot.conf's SOC_MODULES names it. Published as build/dtv_soc.ko too.
dtv_soc_dir = kernel_root / 'dtv_soc'
dtv_soc_inputs = kbuild + [files('Kbuild', 'dtv_soc.c'), webos_tv_h,
                 files('../../../../include/webos-tuner.h')]
dtv_soc_ko = custom_target('dtv_soc',
  output: 'dtv_soc.ko',
  depfile: 'dtv_soc.ko.d',
  depends: [sources['linux']['stamp'], kernel_outputs, webos_tv_ko],
  depend_files: dtv_soc_inputs,
  command: kernel_shared + ['--dir', dtv_soc_dir, '--after', kernel_root, '--after', webos_tv_dir, '--depfile', '@DEPFILE@',
            '--input', dtv_soc_inputs, '--file', dtv_soc_dir / 'dtv_soc.ko', '@OUTPUT@', '--',
            kbuild, 'module', sources['linux']['dir'], kernel_obj, get_option('arm64_cross_compile'),
            meson.current_source_dir(), dtv_soc_dir / 'obj', dtv_soc_dir / 'dtv_soc.ko', webos_tv_symvers],
  console: true,
  build_by_default: true)
published += [['file', dtv_soc_ko.full_path(), 'dtv_soc.ko']]
publish_depends += dtv_soc_ko

