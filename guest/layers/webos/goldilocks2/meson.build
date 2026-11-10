# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The webos layer of goldilocks2, webOS 4.5 and later (W19P's 4.10.2 "goldilocks2-grampians"; 4.0-4.4
# are goldilocks, another release), build/layers/<chip>-goldilocks2/webos, always used: how this
# release's LG userland is put together.
# Its base is goldilocks (base: guest/layers/webos/goldilocks is read first), and it adds nothing: W19P's
# jobs that the goldilocks layer overrides are W18R's byte for byte (tvservice, PmLogDaemon,
# mount-bsp, lginit_app, SetInitEnv's start), its fstabs differ from W18R's only in the partition
# numbers, which the layer's squashfs fstab (init mounts the paks) does not use, and
# its sam is gated as W18R's (TVExtension::OnServiceCountryChanged, which reloads the launch points
# on a country change only in FirstUse). So this layer is goldilocks' as it is: its tree, the
# dreadlocks files it takes and the PmLog preload.
# What goldilocks2 adds over goldilocks (22 jobs: tee-supplicant, home, fullhome, aiservice, voice,
# airplay, alexa, mdnsd, xinetd, dpp, oled-epl, nudge, contentminer, ...; mount-readwrite's
# partialview and webos-jail.sh) starts on its own signals and needs no override, except airplay-adaptor
# (rootfs/etc/init/airplay-adaptor.override, manual: no MFi keys in the TEE, no Wi-Fi).
# The table is goldilocks' too: wayland_server_compat false (W19P's Wayland server is 1.11, as
# W18R's) and dile_gal_capability_bytes 76 (W19P's HAL_GAL_GetDeviceCapability clears 76 and
# writes a byte at +72).

webos_layer_args += ['--tree', meson.current_source_dir() / 'rootfs', '.']
