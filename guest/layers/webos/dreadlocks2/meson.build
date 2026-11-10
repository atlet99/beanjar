# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The webos layer of dreadlocks2, webOS 3.5 and later (W17H's 3.9.3 "dreadlocks2-dudhwa"; 3.0-3.4
# are dreadlocks, another release), build/layers/<chip>-dreadlocks2/webos, always used: how this
# release's LG userland is put together.
# Its base is dreadlocks (base: guest/layers/webos/dreadlocks is read first). W17H's jobs that dreadlocks
# overrides are dreadlocks' byte for byte (mount-bsp, PmLogDaemon, snapshot-resume-jobs,
# tvservice, lginit_app, mknod, bootd), so dreadlocks' files stand:
#   mount-bsp.override, lginit_app.override, tvservice.override, emulator-pmlog-devlog.conf (the
#   devlog switch is there: PmLogDaemon's setdevlogstatus, libPmLogLib's PmLogSetDevMode)
# and this release's own replace the rest, in rootfs/etc/init:
#   emulator-format-state.conf            as dreadlocks' (the same mkfs options), and a new apps
#                                         filesystem gets the factory's preload/ directory, without
#                                         which webOS 3.5's mount-readwrite stops before its
#                                         RWMOUNT_FLAG (tvservice waits for it)
#   mount-readonly-volatile.d/00-partinfo.squashfs.fstab
#                                         the paks, which init mounts
#   emulator-jail-nodes.conf              the nodes of the jailer's "m16p" branch
# The gl layer takes wayland_server_compat from dreadlocks: W17H's Wayland server is 1.7, as W16R's.
# libdile_gal's DILE_GAL_DEVICE_CAPABILITY_INFO_T: 76 bytes (W17H's gfx_GetDeviceCapability)
dile_gal_capability_bytes = 76

webos_layer_args += ['--tree', meson.current_source_dir() / 'rootfs', '.']
