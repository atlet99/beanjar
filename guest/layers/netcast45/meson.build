# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The netcast45 layer, build/layers/lm14-netcast45/netcast45: NetCast 4.5's one always-used layer
# (docs/dev/architecture/netcast45.md), stacked over its stock root (root.pak), built with the NetCast SDK
# (meson/cross/lgtv-sdk.ini):
#   rootfs/etc/emulator/board.cmdline   the bootloader's parameters (targets.py's Soc.board_src)
#   mnt/vendor/libgal.so.0.0.0          the GAL_GX graphics library on the emulator's GOP
#                                       (guest/layers/simplesmart/src/libhalgal built with GAL_GX=1);
#                                       init lays it over the vendor pak, whose libgal.so and
#                                       libgal.so.0 links (lginit's LD_PRELOAD, RELEASE's NEEDED)
#                                       then reach it
#   mnt/lg/res/lglib/libEGL.so.1.4      the hoagx window system over Mesa (src/libegl-hoagx), in place
#                                       of the vendor's Mali libEGL (init lays the layer over the
#                                       lglib pak; the pak's libEGL.so{,.1} links reach it)
#   mnt/lg/res/lglib/libGLESv2.so.2.0, libGLESv1_CM.so.1.1   Mesa's (guest/layers/gl/mesa, NetCast SDK)
#   mnt/lg/res/lglib/libMali.so.0.0     no code: NEEDs libEGL and libGLESv2 (fxui and the browser
#                                       NEED libMali and import nothing of it)
#   usr/lib/libEGLmesa.so.1             Mesa's libEGL, which libEGL loads; libgallium and libgbm,
#                                       which Mesa NEEDs, in /usr/lib: found without any
#                                       LD_LIBRARY_PATH (libdrm is linked into them, statically)
#   usr/share/beanjar/gles-version      OpenGL ES 2.0, the set's own (guest/layers/gl/gles-version.in):
#                                       LM14 ATSC 04.11.18 lgli.pak's libGLESv2.so.2.0 prints
#                                       "OpenGL ES 2.0" (CORES=MALI450 Linux-r3p2-01rel3)
gal = shared_library('gal', '../simplesmart/src/libhalgal/halgal.c', '../simplesmart/src/libhalgal/decode.c',
  '../soc/src/libdile_gal/gal-draw.c',
  include_directories: include_directories('../soc/src/libdile_gal'),
  c_args: ['-fvisibility=hidden', '-DGAL_GX=1'],
  link_args: ['-lpthread', '-ldl'])
appctrl_launch = executable('appctrl-launch', 'src/appctrl-launch.c',
  link_args: ['-ldl'])
appctrl_ready = shared_library('appctrl-ready', 'src/appctrl-ready.c',
  link_args: ['-ldl'])
netcast45_args = ['--tree', meson.current_source_dir() / 'rootfs', '.',
  '--file', gal, 'mnt/vendor/libgal.so.0.0.0', '755',
  '--file', appctrl_launch, 'etc/emulator/appctrl-launch', '755',
  '--file', appctrl_ready, 'etc/emulator/libappctrl-ready.so', '755']

mesa_ver = sources['mesa']['version']
egl = shared_library('EGL', 'src/libegl-hoagx/egl.c',
  dependencies: mesa_dep,
  c_args: ['-fvisibility=hidden'],
  link_args: ['-Wl,-soname,libEGL.so', '-Wl,--hash-style=both', '-ldl', '-lpthread'],
  link_depends: mesa)
mali = shared_library('Mali', '../gl/src/pvr-stub.c',
  link_args: ['-nostdlib', '-L' + mesa_lib, '-Wl,--no-as-needed', '-Wl,-soname,libMali.so',
              egl.full_path(), '-lGLESv2'],
  link_depends: [mesa, egl])
L = 'usr/lib/'
G = 'mnt/lg/res/lglib/'     # over the lglib pak (init's overlay): the vendor's Mali libraries' names
netcast45_args += [
  '--file', egl, G + 'libEGL.so.1.4', '755',
  '--file', mesa_lib / 'libGLESv2.so.2.0.0', G + 'libGLESv2.so.2.0', '755',
  '--file', mesa_lib / 'libGLESv1_CM.so.1.1.0', G + 'libGLESv1_CM.so.1.1', '755',
  '--file', mali, G + 'libMali.so.0.0', '755',
  '--file', mesa_lib / 'libEGLmesa.so.1.0.0', L + 'libEGLmesa.so.1', '755',
  '--file', mesa_lib / ('libgallium-' + mesa_ver + '.so'), L + 'libgallium-' + mesa_ver + '.so', '755',
  '--file', mesa_lib / 'libgbm.so.1.0.0', L + 'libgbm.so.1', '755',
  '--file', configure_file(input: '../gl/gles-version.in', output: 'gles-version',
    configuration: {'GLES': '2.0'}), 'usr/share/beanjar/gles-version', '644',
]

layers += {'netcast45': custom_target('netcast45-layer',
  output: 'netcast45-layer.stamp',
  depfile: 'netcast45-layer.d',
  depends: mesa,
  command: arm_stage + ['--out', layers_dir / 'netcast45', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@']
    + netcast45_args,
  build_by_default: true)}
