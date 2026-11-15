# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The testguest layer, build/layers/virt-testguest/testguest: the test guest's one always-used layer
# (docs/dev/architecture/test-guest.md), over the vendored root filesystem. The checks of src/, built
# against the vendored sysroot (the cross file scripts/testguest-setup writes names it), and
# rootfs/: the runner tg-run, its init script and the board file (targets.py's Soc.board_src).
#   usr/bin/tg-gpu      GBM, EGL and GLES on the render node (virgl)
#   usr/bin/tg-ports    the virtio-serial ports and the media and video wire formats (include/)
#   usr/bin/tg-input    a key from QEMU's virtio keyboard
#   usr/bin/tg-bridge   the 32-bit ABI through the kernel's compat table
tg_inc = include_directories('../../include')
tg_args = ['-Wall', '-Wextra', '-O2']
tg_gpu = executable('tg-gpu', 'src/tg-gpu.c',
  c_args: tg_args,
  dependencies: [dependency('egl'), dependency('glesv2'), dependency('gbm')])
tg_ports = executable('tg-ports', 'src/tg-ports.c',
  c_args: tg_args,
  include_directories: tg_inc)
tg_input = executable('tg-input', 'src/tg-input.c',
  c_args: tg_args)
tg_bridge = executable('tg-bridge', 'src/tg-bridge.c',
  c_args: tg_args,
  dependencies: dependency('threads'))
testguest_args = ['--tree', meson.current_source_dir() / 'rootfs', '.']
foreach exe : [tg_gpu, tg_ports, tg_input, tg_bridge]
  testguest_args += ['--file', exe, 'usr/bin/' + exe.name(), '755']
endforeach
layers += {'testguest': custom_target('testguest-layer',
  output: 'testguest-layer.stamp',
  depfile: 'testguest-layer.d',
  command: arm_stage + ['--out', layers_dir / 'testguest', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@']
    + testguest_args,
  build_by_default: true)}
layer_meta += {'testguest': meson.current_source_dir() / 'layer.toml'}
