# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The webos layer of jhericurl, webOS 5.x (W20L's 5.6.1 "jhericurl-jimna"), the first systemd release,
# build/layers/<chip>-jhericurl/webos, always used: how this release's LG userland is put together.
# Not
# based on goldilocks: an Upstart override means nothing to systemd. Its changes are drop-ins
# (etc/systemd/system/<unit>.d/emulator-*.conf) and the stock units' configuration directories,
# in rootfs/etc/systemd/system:
#   bsp-mounted.path.d/emulator-bsp.conf    the path unit that starts local-fs-pre-starfish.target
#                                           watches the initramfs's /run/emulator/bsp-mounted
#   mount-bsp.service.d/emulator-bsp.conf   the unit does nothing: bsppart is a lower layer
#   mount-readonly-volatile.d/00-partinfo.squashfs.fstab
#                                           the paks, which init mounts (no cert: no pak)
# The state disks are mounted by the stock fstab (/dev/mmcblk0pN).
# and usr/lib/libpmlog-keep-all.so, the PmLog preload (guest/layers/webos/src/pmlogd) that init's
# drop-in for pm-log-daemon.service loads (PmLog mode "preload-systemd", targets.py).
# The firmware's init (targets.py's Release.init_system): guest/layers/headless builds the compositor
# stand-in for systemd.
init_system = 'systemd'
# The gl layer's wayland_server_compat: W20L's Wayland server is 1.11, as W18R's.
wayland_server_compat = false
# libdile_gal's capability: W20L's libhalgal's HAL_GAL_GetDeviceCapability still clears 76 bytes.
dile_gal_capability_bytes = 76

webos_layer_args += ['--tree', meson.current_source_dir() / 'rootfs', '.',
  '--file', pmlog_keep_all, 'usr/lib/libpmlog-keep-all.so', '755']
