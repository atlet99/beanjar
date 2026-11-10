#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Run after pulseaudio-ptts.sh; omit only the unavailable card-1 AEC module.
set -eu
fail() { echo "emulator-pulseaudio-mic-provider: $*" >&2; exit 1; }
[ "$#" -eq 0 ] || [ "$#" -eq 3 ] || fail 'expected no arguments or config/runtime/sound paths'
f=${1:-/etc/pulse/system.pa}
r=${2:-/run/beanjar-pulse}
sound=${3:-/sys/class/sound}
[ -d "$sound" ] || fail 'ALSA sysfs directory unavailable'
[ -r "$f" ] || fail 'system.pa is not readable'
mkdir -p "$r" || fail 'cannot create runtime directory'
tmp=$(mktemp "$r/system.pa.XXXXXX") || fail 'cannot create temporary configuration'
trap 'rm -f "$tmp"' 0
trap 'exit 1' HUP INT TERM
# Undo only our own prefix before reevaluating card presence. Keep arguments intact.
filter=''
if [ ! -d "$sound/card1" ]; then
    filter='/^[[:space:]]*load-module[[:space:]][[:space:]]*module-mic-capture\([[:space:]].*\)\{0,1\}$/s/^/# beanjar-no-card1: /'
fi
sed -e 's/^# beanjar-no-card1: //' -e "$filter" "$f" > "$tmp" || fail 'cannot generate configuration'
# Stock SquashFS system.pa is root:root 0644. PulseAudio drops privileges
# before reading it; mktemp's 0600 would hide all configured modules.
chmod 0644 "$tmp" || fail 'cannot set configuration read permissions'
if cmp -s "$tmp" "$f"; then
    # Also repair a binding left by an earlier helper with mktemp permissions.
    if [ "$f" -ef "$r/system.pa" ]; then
        chmod 0644 "$r/system.pa" || fail 'cannot repair configuration read permissions'
    fi
    exit 0
fi
out=$r/system.pa
if [ "$f" -ef "$out" ]; then
    # On a provider transition, remove only our top binding. The inherited
    # ptts configuration remains beneath it. Never truncate a bound file or
    # stack a second owned mount; failures stop the service pre-step.
    umount "$f" || fail 'cannot unbind previous runtime configuration'
fi
mv "$tmp" "$out" || fail 'cannot write runtime configuration'
mount --bind "$out" "$f" || fail 'cannot bind runtime configuration'
if [ -d "$sound/card1" ]; then
    echo 'emulator-pulseaudio-mic-provider: card1 present; stock mic module retained'
else
    echo 'emulator-pulseaudio-mic-provider: no card1; AEC mic module omitted (capture unavailable)'
fi
