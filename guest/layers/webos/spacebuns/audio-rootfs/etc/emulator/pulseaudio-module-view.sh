#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# AEC needs card1. Hide only that unavailable plugin from client-requested loads.
set -eu
fail() { echo "emulator-pulseaudio-module-view: $*" >&2; exit 1; }
[ "$#" -eq 0 ] || [ "$#" -eq 3 ] || fail 'expected no arguments or modules/runtime/sound paths'
modules=${1:-/usr/lib/pulse-15.0/modules}
r=${2:-/run/beanjar-pulse}
sound=${3:-/sys/class/sound}
[ -d "$modules" ] || fail 'stock module directory unavailable'
[ -d "$sound" ] || fail 'ALSA sysfs directory unavailable'
modules=$(cd "$modules" && pwd -P) || fail 'cannot resolve stock module directory'
omit_mic=1
[ ! -d "$sound/card1" ] || omit_mic=0
mkdir -p "$r" || fail 'cannot create runtime directory'
old=''
if [ -e "$r/modules" ] || [ -L "$r/modules" ]; then
    [ -L "$r/modules" ] || fail 'runtime module view is not an owned symlink'
    old=$(readlink "$r/modules") || fail 'cannot read runtime module view'
    case "$old" in modules.*) ;; *) fail 'runtime module view is not owned' ;; esac
    case "$old" in */*) fail 'runtime module view is not owned' ;; esac
    [ -d "$r/$old" ] && [ ! -L "$r/$old" ] || fail 'previous module view unavailable'
fi
tmp=$(mktemp -d "$r/modules.XXXXXX") || fail 'cannot create module view'
link=$tmp.link
cleanup() {
    [ -z "$link" ] || rm -f "$link"
    if [ -n "$tmp" ] && [ "$(readlink "$r/modules" 2>/dev/null || :)" != "${tmp##*/}" ]; then
        rm -rf "$tmp"
    fi
}
trap cleanup 0
trap 'exit 1' HUP INT TERM
# mktemp creates 0700 directories; PulseAudio drops privileges before loading.
chmod 0755 "$tmp" || fail 'cannot set module view permissions'
count=0
for src in "$modules"/*; do
    [ -e "$src" ] || [ -L "$src" ] || continue
    name=${src##*/}
    if [ "$omit_mic" -eq 1 ]; then
        case "$name" in module-mic-capture.so|module-mic-capture.so.*|module-mic-capture.la) continue ;; esac
    fi
    ln -s "$src" "$tmp/$name" || fail 'cannot link stock module'
    count=$((count + 1))
done
[ "$count" -gt 0 ] || fail 'module view contains no available modules'
# Preserve the published view on an unchanged restart.
same=1
if [ -n "$old" ]; then
    for dir in "$tmp" "$r/$old"; do
        for entry in "$dir"/*; do
            [ -L "$entry" ] || { same=0; break; }
            name=${entry##*/}
            [ -L "$tmp/$name" ] && [ -L "$r/$old/$name" ] &&
                [ "$(readlink "$tmp/$name")" = "$(readlink "$r/$old/$name")" ] || same=0
        done
    done
    [ "$same" -eq 0 ] || exit 0
fi
# Atomically publish a complete view; failed construction leaves the old one intact.
ln -s "${tmp##*/}" "$link" || fail 'cannot prepare module view link'
mv -fT "$link" "$r/modules" || fail 'cannot publish module view'
tmp=''
link=''
[ -z "$old" ] || rm -rf "$r/$old" || fail 'cannot remove previous module view'
