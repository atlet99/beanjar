#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Guest half of tests/regress/audio/check-volume.sh; runs inside webOS (webos-enter) from the VM's share
# (`host/scripts/vm-launcher put`). Plays TONE through pulseaudio at several master volumes set through audiod
# (com.webos.audio), which forwards them to tvservice's tv.sound; its audio HAL sets pcm_output.
#   sh /mnt/host/tests/regress/audio/guest-volume.sh /mnt/host/out/tone.wav
TONE=${1:?tone wav}
ls() { luna-send -n 1 "luna://$1" "$2" </dev/null; echo; }
sink() { pactl list sinks | sed -n '/Name: pcm_output/,/Base Volume/p' | grep -E '^\s*(Mute|Volume):' | tr -s ' \t' ' ' | tr '\n' ';'; echo; }
play() { echo "play: $1 | $(sink)"; paplay "$TONE"; sleep 1; }
ls com.webos.service.tv.sound/private/getCurrentSoundOutput '{}'
ls com.webos.audio/setVolume '{"volume":100}'; sleep 1; play "volume 100"
ls com.webos.audio/setVolume '{"volume":50}'; sleep 1; play "volume 50"
ls com.webos.audio/setVolume '{"volume":10}'; sleep 1; play "volume 10"
ls com.webos.audio/setMuted '{"muted":true}'; sleep 1; play "muted"
ls com.webos.audio/setMuted '{"muted":false}'
ls com.webos.audio/volumeUp '{}'; sleep 1; play "volumeUp"
ls com.webos.service.tv.sound/private/getVolume '{"tvSoundOutput":"tv_speaker"}'
ls com.webos.service.tv.sound/private/getMuteStatus '{"tvSoundOutput":"tv_speaker"}'
ls com.webos.service.tv.sound/getVolume '{}'
ls com.webos.service.tv.sound/getMuteStatus '{}'
ls com.webos.audio/getVolume '{}'
echo "GUEST-VOLUME-DONE"
