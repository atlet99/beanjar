# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The webos layer of afro, webOS 1.x (e.g. WT1H's 1.4.0 "afro-ashley"), build/layers/<chip>-afro/webos, always
# used: how this release's LG userland is put together. Only files so far, all under rootfs/etc/init unless noted:
#   emulator-format-state.conf            formats a blank state disk before mount-readwrite
#   mount-readonly-volatile.d/00-partinfo.squashfs.fstab
#                                         the paks, which init mounts (the stock ext4 fstab mounts the state disks)
#   lginit_app.override, securitymanager.override, emulator-jail-nodes.conf
#   usr/lib/libpmlog-keep-all.so          PmLogDaemon's whitelist off (guest/layers/webos/src/pmlogd), this
#                                         release's PmLog mode "preload" (targets.py, RELEASES)
#   tvservice.override                    the stock job's script with libtvservice-private.so
#                                         (guest/layers/common/tvservice) LD_PRELOADed after dbgfrwk's
#                                         liblibm
#   tvwin.override, usr/share/dbus-1/{services,system-services}/com.webos.service.tvwin.service
#                                         the same for tvwin, the other process that links
#                                         libhal_lg115x, in its factory job and as the on-demand
#                                         LS2 service (the service remote's factory keys)
# The gl layer (guest/layers/gl) takes wayland_server_compat from here: the release's Wayland server
# (1.0.3) needs the newer server API over it for Mesa.
wayland_server_compat = true

webos_layer_args += ['--tree', meson.current_source_dir() / 'rootfs', '.',
  '--file', pmlog_keep_all, 'usr/lib/libpmlog-keep-all.so', '755']
