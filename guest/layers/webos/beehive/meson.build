# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The webos layer of beehive, webOS 2.x (e.g. W15M's 2.2.3 "beehive-biscayne"),
# build/layers/<chip>-beehive/webos, always used: how this release's LG userland is put together.
# Derived from W15M's Upstart jobs, which
# keep webOS 1's critical path (ls-hubd, lginit_app, tvservice, gpu-libs, surface-manager, sam and
# WebAppMgr, bootd). Only files so far, all under rootfs/etc/init:
#   emulator-format-state.conf            formats a blank state disk before mount-readwrite, with
#                                         this release's mkfs options (-E nodiscard)
#   mount-readonly-volatile.d/00-partinfo.squashfs.fstab
#                                         the paks, which init mounts at webOS 1's mount points
#   lginit_app.override                   as webOS 1's, and without the job's new reboot post-stop
#   emulator-jail-nodes.conf              the nodes of the jailer's "m14tv" branch
#   usr/lib/libpmlog-keep-all.so          as webOS 1's (PmLog mode "preload")
#   tvservice.override                    the stock job's script with libtvservice-private.so
#                                         (guest/layers/common/tvservice) LD_PRELOADed after dbgfrwk's
#                                         liblibm
# Not here, unlike webOS 1: tvwin (no tvwin job or binary in W15M; factorywin, its successor,
# links neither libhal_lg115x nor anything of tvservice's private view), and securitymanager's
# override, which on webOS 1 is a no-op kept for its notes: W15M's securitymanager has the same
# sb_verify_task gating on /proc/cmdline ("snapshot" and not "fullverify"), which board.cmdline
# never meets, so its stock job runs. PmLogDaemon's job keeps its name (guest/initramfs/rootfs/init's override).
# The gl layer (guest/layers/gl) takes wayland_server_compat from here: the release's Wayland server
# (1.1) needs the newer server API over it for Mesa, as 1.0.3 does.
wayland_server_compat = true

webos_layer_args += ['--tree', meson.current_source_dir() / 'rootfs', '.',
  '--file', pmlog_keep_all, 'usr/lib/libpmlog-keep-all.so', '755']
