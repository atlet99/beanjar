#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Boot timing. Runs in the initramfs debug shell right after boot:
#   sh /host/guest/scripts/time-boot.sh
# Waits until bootd reports boot-done, runs check-m1.sh, and prints guest uptimes for both.
# The check output goes to /tmp/check-m1.out.
until webos-enter luna-send -n 1 luna://com.webos.bootManager/getBootStatus '{}' 2>/dev/null |
	grep -q '"boot-done":true'; do
	sleep 0.2
done
echo "BOOTDONE $(cut -d' ' -f1 /proc/uptime)"
webos-enter sh /mnt/host/guest/scripts/check-m1.sh > /tmp/check-m1.out 2>&1
webos-enter luna-send -n 1 luna://com.webos.applicationManager/listApps '{}' | head -c 60; echo
echo "M1DONE $(cut -d' ' -f1 /proc/uptime)"
