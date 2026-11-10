# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# dtv_soc.ko, the SoC's demodulator output and demux, fed from the host (include/webos-tuner.h):
# built as webos_tv.ko is, against its exports, carried in the initramfs and loaded after it when
# boot.conf's SOC_MODULES names it. Published as build/dtv_soc.ko too.
dtv_soc_ko = custom_target('dtv_soc',
  output: 'dtv_soc.ko',
  depends: [sources['linux']['stamp'], kernel_outputs, webos_tv_ko],
  depend_files: [kbuild, files('Kbuild', 'dtv_soc.c'), webos_tv_h,
                 '../../../../include/webos-tuner.h'],
  command: [kbuild, 'module', sources['linux']['dir'], kernel_obj,
            get_option('arm64_cross_compile'), meson.current_source_dir(),
            meson.current_build_dir() / 'obj', '@OUTPUT@', webos_tv_symvers],
  console: true,
  build_by_default: true)
published += [['file', dtv_soc_ko.full_path(), 'dtv_soc.ko']]
publish_depends += dtv_soc_ko

