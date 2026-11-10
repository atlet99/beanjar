#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# pulseaudio.service's ExecStartPre (pulseaudio.service.d/emulator-ptts.conf): with no kernel PCM
# hw:0,7 (not /dev/snd: the device table makes the nodes with or without one), bind-mount over
# /etc/pulse/system.pa a copy whose ptts sink is a null sink, once (a restart finds it done).
f=/etc/pulse/system.pa
[ -e /sys/class/sound/pcmC0D7p ] && exit 0
grep -q '^load-module module-alsa-sink device=hw:0,7 .*sink_name=ptts' $f || exit 0
sed 's/^load-module module-alsa-sink device=hw:0,7 .*sink_name=ptts.*/load-module module-null-sink sink_name=ptts/' \
	$f > /run/emulator-system.pa
mount --bind /run/emulator-system.pa $f
echo "emulator-pulseaudio-ptts: ptts is a null sink (no hw:0,7)"
