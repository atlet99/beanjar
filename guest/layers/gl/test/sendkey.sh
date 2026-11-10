#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Press and release one key on an input device, from the initramfs debug shell (64-bit busybox,
# so struct input_event is 24 bytes). Writing to an evdev node injects the event into that
# device, and libim reads the devices whose names it knows ("LGE ..."). The event only gets
# through if the device declares the key; our "LGE RCU" declares all of them.
#   (the VM gets it with `host/scripts/vm-launcher put guest/layers/gl/test/sendkey.sh` on the host)
#   sh /host/guest/layers/gl/test/sendkey.sh [CODE] [DEVICE-NAME]
# CODE defaults to 125 (KEY_LEFTMETA, the webOS Home key, which opens the launcher).
c=${1:-125}
name=${2:-LGE RCU}
ev=$(awk -v n="N: Name=\"$name\"" '$0 == n { f = 1 } f && /^H: Handlers=/ { for (i = 2; i <= NF; i++) if ($i ~ /^event/) { print $i; exit } }' /proc/bus/input/devices)
[ -n "$ev" ] || { echo "no input device named '$name'" >&2; exit 1; }
d=/dev/input/$ev
o=$(printf '%03o' $((c & 255))); h=$(printf '%03o' $((c >> 8)))
z='\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000'   # struct timeval
key() { printf "$z\\001\\000\\$o\\$h\\$1\\000\\000\\000"; }              # EV_KEY code value
syn() { printf "$z\\000\\000\\000\\000\\000\\000\\000\\000"; }          # EV_SYN
{ key 001; syn; } > "$d"
sleep 0.2
{ key 000; syn; } > "$d"
echo "key $c -> $d ($name)"
