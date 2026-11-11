# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The TCG benchmark programs (README.md), build/tcg-perf/{perf-arm,io-perf}, and the optional LS2
# role layer tcg-perf (LAYERS=...,tcg-perf) with tcg-luna-perf.
guest_programs += {
  'tcg-perf/perf-arm': executable('perf-arm', 'perf.c', link_args: ['-static', '-lrt']),
  'tcg-perf/io-perf': executable('io-perf', 'io-perf.c', link_args: ['-static', '-lrt']),
}
# against the firmware's LS2 and GLib: their stubs (fw_stubs, guest/targets/meson.build)
luna_link = []
luna_link_depends = []
foreach lib : ['usr/lib/libluna-service2.so.3', 'usr/lib/libglib-2.0.so.0']
  fw_questions += 'lib ' + lib
  if lib in fw_stubs
    luna_link += fw_stubs[lib].full_path()
    luna_link_depends += fw_stubs[lib]
  endif
endforeach
luna_perf = executable('tcg-luna-perf', 'luna-perf.c',
  link_args: luna_link + ['-Wl,--allow-shlib-undefined', '-lrt', '-ldl'],
  link_depends: luna_link_depends)
layers += {'tcg-perf': custom_target('tcg-perf-layer',
  output: 'tcg-perf-layer.stamp',
  depfile: 'tcg-perf-layer.d',
  command: arm_stage + ['--out', layers_dir / 'tcg-perf', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@',
    '--tree', meson.current_source_dir() / 'layer', '.',
    '--file', luna_perf, 'usr/sbin/tcg-luna-perf', '755'],
  build_by_default: true)}
layer_meta += {'tcg-perf': meson.current_source_dir() / 'layer.toml'}

