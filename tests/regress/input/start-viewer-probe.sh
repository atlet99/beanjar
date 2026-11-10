#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Run from the initramfs debug shell (tests/regress/input/check-viewer.py puts it, run-pointer-probe.sh and
# pointer.qml into the VM's share first). All files/processes are VM test artifacts; the captures
# go to the output share, /host/out.
set -eu
webos-enter sh -c 'wc -l </var/log/messages' >/run/viewer-log-lines
if [ -f /run/viewer-probe.pid ]; then
    kill "$(cat /run/viewer-probe.pid)" 2>/dev/null || true
fi
webos-enter sh /mnt/host/tests/regress/input/run-pointer-probe.sh >/run/pointer-probe.log 2>&1 &
echo $! >/run/viewer-probe.pid
# the pointer bridge's device, the PC keyboard (QEMU's virtio keyboard, read by libim as a HID
# keyboard) and tvservice's remote device (the micom's keys)
for pair in 'LGE M-RCU - Builtin [3]:pointer' 'QEMU Virtio Keyboard:keyboard' 'LGE RCU:remote'; do
    name=${pair%:*}; kind=${pair##*:}
    ev=$(awk -v n="N: Name=\"$name\"" '$0 == n { f=1 } f && /^H:/ { if (match($0,/event[0-9]+/)) print substr($0,RSTART,RLENGTH); exit }' /proc/bus/input/devices)
    [ -n "$ev" ] || { echo "missing $name"; exit 1; }
    timeout 12 cat "/dev/input/$ev" >"/host/out/viewer-$kind.bin" &
done
sleep 1
echo ready >/host/out/viewer-probe.ready
