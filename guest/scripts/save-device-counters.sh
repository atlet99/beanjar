#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Snapshot only public request counters, not identity or firmware memory files.
# Usage: sh save-device-counters.sh OUT [PROC_ROOT] (PROC_ROOT is for fixture tests).
set -u
out=$1
proc=${2:-/proc}
mkdir -p "$out" || exit 1
inventory=$out/inventory.tsv
: > "$inventory" || exit 1
# Old firmware BusyBox uses -t; the outer initramfs uses positional duration.
if timeout -t 1 sh -c ':' >/dev/null 2>&1; then
    timeout_style=old
elif timeout 1 sh -c ':' >/dev/null 2>&1; then
    timeout_style=positional
else
    timeout_style=unavailable
fi
read_counter() {
    if [ "$timeout_style" = old ]; then
        timeout -t 5 cat "$1"
    else
        timeout 5 cat "$1"
    fi
}
for entry in 'mstar-utopia/ioctls:mstar-ioctls' 'utopia:utopia' \
    'mstar-utopia/tee:mstar-tee' 'webos_tv/props:webos-properties' \
    'rtk/ioctl:rtk-ioctls' 'rtk/rpc:rtk-rpc' 'rtk/tee:rtk-tee'; do
    path=${entry%:*}; key=${entry#*:}
    rm -f "$out/$key.txt" "$out/$key.error"
    if [ ! -e "$proc/$path" ]; then
        status=absent
    elif [ ! -r "$proc/$path" ]; then
        status=unreadable
    elif [ "$timeout_style" = unavailable ]; then
        status=timeout-unavailable
    elif read_counter "$proc/$path" > "$out/$key.tmp" 2> "$out/$key.error"; then
        if mv "$out/$key.tmp" "$out/$key.txt"; then
            status=available
            rm -f "$out/$key.error"
        else
            status=write-failed
        fi
    else
        status=read-failed
    fi
    rm -f "$out/$key.tmp"
    printf '%s\t%s\t%s\n' "/proc/$path" "$status" "$key.txt" >> "$inventory" || exit 1
done
