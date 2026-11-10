#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Run inside webOS after boot; check correctness before timing repeated service calls.
sh /mnt/host/guest/scripts/check-m1.sh 150
count=${1:-100}
for repeat in 1 2 3; do
    read start rest < /proc/uptime
    i=0
    while [ "$i" -lt "$count" ]; do
        reply=$(luna-send -n 1 luna://com.webos.service.tvpower/power/getPowerState '{}')
        case $reply in *'"state":"Active"'*|*'"state": "Active"'*) ;; *) echo "SERVICE FAILED: $reply"; exit 1 ;; esac
        i=$((i+1))
    done
    read end rest < /proc/uptime
    echo "service$count start=$start end=$end"
done
if [ -x /usr/sbin/tcg-luna-perf ]; then
    for repeat in 1 2 3; do
        /usr/sbin/tcg-luna-perf || exit 1
    done
fi
