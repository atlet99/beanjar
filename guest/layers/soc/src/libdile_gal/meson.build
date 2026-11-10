# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/<soname> of hal_stock['libdile_gal'] (libdile_gal.so.1 on H13, M14 and M2): replaces the
# stock symlink, with that SONAME, and NEEDs the stock file (unchanged). Loaded by gm, libgm's
# clients, libim's and tvservice. DILE_GAL on the emulator's graphics memory:
# surfaces for every process in the shared /dev/gfx
# (gfx-memory.c; the SoC kernel module's gfx node, lgemu.ko's gfx.c), a software 2D engine (gal-draw.c) and PNG decoder (gal-image.c), and the frame buffer controllers, whose cursor plane goes to the viewer
# (cursor-channel.c: webos.cursor) and whose OSD plane the compositor's egl-kms scans out, with
# the compositor's place in gm's merges (gfx-merge.c) and its pictures read back (osd-readback.c).
dile_gal_stock = shared_library('libdile_gal', hal_link_stub,
  name_prefix: '', name_suffix: hal_stock['libdile_gal'][1].substring(12))
dile_gal = shared_library('dile_gal', 'dile-gal.c', 'gfx-memory.c', 'gfx-merge.c', 'gal-draw.c',
  'gal-image.c', 'cursor-channel.c', 'osd-readback.c',
  soversion: hal_stock['libdile_gal'][0].split('.so.')[1],
  c_args: ['-fvisibility=hidden',
    # the release's DILE_GAL capability size (guest/layers/webos/<release>; 72 where it does not say)
    '-DGAL_CAPABILITY_BYTES=@0@'.format(get_variable('dile_gal_capability_bytes', 72))],
  link_args: ['-Wl,--no-as-needed', dile_gal_stock.full_path(), '-ldl', '-lpthread'],
  link_depends: dile_gal_stock)
hal_libs += {hal_stock['libdile_gal'][0]: dile_gal}
hal_libs_stock += {hal_stock['libdile_gal'][0]: hal_stock['libdile_gal'][1]}
