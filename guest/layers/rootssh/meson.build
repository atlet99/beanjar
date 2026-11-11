# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The rootssh layer, build/layers/<chip>-<release>/rootssh: key-only root SSH (LAYERS=...,rootssh), from rootfs/ and
# the guest's SSH programs (guest/tools/ssh). Made by build-layer.sh. It holds no key: the host key and
# root's authorized_keys are each VM's own, in its identity directory, and the launcher brings them
# in through the per-VM layer.
layers += {'rootssh': custom_target('rootssh-layer',
  output: 'rootssh-layer.stamp',
  depfile: 'rootssh-layer.d',
  depends: ssh_bin,
  command: [python, layer_script_py, '--dest', layers_dir / 'rootssh', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@',
    '--dep', meson.current_source_dir() / 'build-layer.sh',
    '--dep-tree', meson.current_source_dir() / 'rootfs', '--dep', ssh_bin.full_path(),
    '--', 'env', 'BIN=' + ssh_bin_dir, meson.current_source_dir() / 'build-layer.sh'],
  build_by_default: true)}
