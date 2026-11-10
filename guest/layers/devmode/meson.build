# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The devmode layer, build/layers/<chip>-<release>/devmode: LG Developer Mode (LAYERS=...,devmode). Made by
# build-layer.sh from rootfs/ and the guest's SSH programs (guest/tools/ssh). It holds no key: the
# developer and host keys are each VM's own, in its identity directory, and the launcher brings
# them in through the per-VM layer.
layers += {'devmode': custom_target('devmode-layer',
  output: 'devmode-layer.stamp',
  depfile: 'devmode-layer.d',
  depends: ssh_bin,
  command: layer_script + ['--dest', layers_dir / 'devmode', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@',
    '--dep', meson.current_source_dir() / 'build-layer.sh', '--dep', ssh_bin.full_path(),
    '--dep-tree', meson.current_source_dir() / 'rootfs',
    '--', 'env', 'BIN=' + ssh_bin_dir, meson.current_source_dir() / 'build-layer.sh'],
  build_by_default: true)}
