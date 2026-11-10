# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The TCG benchmark programs (README.md), build/tcg-perf/{perf-arm,io-perf}, and the optional LS2
# role layer tcg-perf (LAYERS=...,tcg-perf) with tcg-luna-perf.
guest_programs += {
  'tcg-perf/perf-arm': executable('perf-arm', 'perf.c', link_args: ['-static', '-lrt']),
  'tcg-perf/io-perf': executable('io-perf', 'io-perf.c', link_args: ['-static', '-lrt']),
}
luna_perf = executable('tcg-luna-perf', 'luna-perf.c',
  link_args: [fwlib / 'libluna-service2.so.3', fwlib / 'libglib-2.0.so.0',
              '-Wl,--allow-shlib-undefined', '-lrt', '-ldl'])
layers += {'tcg-perf': custom_target('tcg-perf-layer',
  output: 'tcg-perf-layer.stamp',
  depfile: 'tcg-perf-layer.d',
  command: arm_stage + ['--out', layers_dir / 'tcg-perf', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@',
    '--tree', meson.current_source_dir() / 'layer', '.',
    '--file', luna_perf, 'usr/sbin/tcg-luna-perf', '755'],
  build_by_default: true)}
layer_meta += {'tcg-perf': meson.current_source_dir() / 'layer.toml'}

