#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
mkdir -p /iotest
for serial in /sys/block/vd*/serial; do
    if [ "$(cat "$serial")" = ioperf ]; then
        dev=${serial%/serial}
        mount -t ext4 -o ro /dev/${dev##*/} /iotest
    fi
done
for round in 1 2 3; do
    for file in /lower/rootfs/usr/lib/libQt5WebKit.so.5.0.0 /iotest/libQt5WebKit.so.5.0.0; do
        echo 3 > /proc/sys/vm/drop_caches
        echo "io round=$round cold"
        /host/build/tcg-perf/io-perf "$file"
        echo "io round=$round warm"
        /host/build/tcg-perf/io-perf "$file"
    done
done
umount /iotest
