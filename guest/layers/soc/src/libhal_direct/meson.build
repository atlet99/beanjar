# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libhal_vdec_direct'] and hal_stock['libhal_audio_direct'] (bsppart's
# direct video decoder and audio output, which LG's libdile_vdec_direct and libdile_audio_direct
# run over): each replaces the stock symlink and NEEDs the stock file (unchanged). Every HAL call
# DILE makes resolves here, and the streams go to the host over the media ports
# (hal-vdec-direct.c, hal-audio-direct.c; ../video/src/mediaport.c without GStreamer). Built after
# src/video, whose GLib version they share.
direct_args = ['-DMEDIAPORT_CORE', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_' + gst_set['glib'],
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_' + gst_set['glib']]
direct_link = ['-Wl,-z,defs', '-Wl,--allow-shlib-undefined', '-Wl,--as-needed', '-pthread']
direct_link_depends = []
fw_questions += 'lib usr/lib/libglib-2.0.so.0'
if 'usr/lib/libglib-2.0.so.0' in fw_stubs
  direct_link += fw_stubs['usr/lib/libglib-2.0.so.0'].full_path()
  direct_link_depends += fw_stubs['usr/lib/libglib-2.0.so.0']
endif
# What LG's libdile_vdec_direct and libdile_audio_direct import of each (W17H, W18R, W19P alike):
# every one must resolve here, never in the stock HAL (test soc-<name>-exports).
direct_imports = {
  'libhal_vdec_direct': ['Open', 'Play', 'PlayWithCallback', 'SetCallback', 'SetDEId', 'GetVideoInfo',
                         'Stop', 'Close'],
  'libhal_audio_direct': ['Open', 'Start', 'Write', 'Stop', 'Close', 'Pause', 'Resume', 'Flush',
                          'GetBufferInfo', 'SetNoDelayParam'],
}
foreach name, source : {'libhal_vdec_direct': 'hal-vdec-direct.c', 'libhal_audio_direct': 'hal-audio-direct.c'}
  if name in hal_stock
    sonames = hal_stock[name]
    base = sonames[0].split('.so')[0]
    stock = shared_library(base, hal_link_stub,
      name_prefix: '', name_suffix: sonames[1].replace(base + '.', ''))
    shim_soname = sonames[0].split('.so.')
    shim = shared_library(base.substring(3), source, '../video/src/mediaport.c',
      c_args: direct_args,
      dependencies: glib_headers,
      kwargs: shim_soname.length() > 1 ? {'soversion': shim_soname[1]} : {},
      link_args: direct_link + ['-Wl,--no-as-needed', stock.full_path()],
      link_depends: direct_link_depends + [stock])
    hal_libs += {sonames[0]: shim}
    hal_libs_stock += {sonames[0]: sonames[1]}
    prefix = name == 'libhal_vdec_direct' ? 'HAL_VDEC_DIRECT_' : 'HAL_AUDIO_DIRECT_'
    want = []
    foreach f : direct_imports[name]
      want += prefix + f
    endforeach
    test('soc-' + name + '-exports', find_program('sh', native: true),
      args: ['-c', 'syms=$("$1" --dyn-syms -W "$2" | awk \'$7 != "UND" { print $8 }\'); shift 2; '
        + 'for s; do echo "$syms" | grep -qx "$s" || { echo "missing $s"; exit 1; }; done',
        'sh', readelf, shim] + want,
      suite: 'layers')
  endif
endforeach
