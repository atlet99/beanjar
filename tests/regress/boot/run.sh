#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# One timed boot: start the VM, wait for bootd's last signal, optionally time an app's first
# frame, run check-m1.sh, stop the VM, print the timeline summary.
#   tests/regress/boot/run.sh NAME [APP [SETTLE]]
# With host/scripts/vm-launcher start/stop: KVM on the Pi, TCG elsewhere. The launcher's
# environment passes through (LAYERS, TCG_CPU, SMP, ROOTFS, PMLOG_DEBUG, QEMU_EXTRA, ...);
# STATE defaults to none and NET_LINK to off here, so every run starts from the same fresh state.
# Logs: build/logs/NAME (the guest writes them to its out/logs, the stop moves them); the console
# output and summary: build/boot-times/NAME.{out,json}.
set -u
cd "$(dirname "$0")/../../.."
name=$1 app=${2:-} settle=${3:-20}
export STATE=${STATE:-none} NET_LINK=${NET_LINK:-off}
mkdir -p build/boot-times
rm -rf "build/logs/$name"
./host/scripts/vm-launcher start >/dev/null || exit 1
./host/scripts/vm-launcher put tests/regress/boot/guest.sh >/dev/null || exit 1
./host/scripts/vm-launcher console "sh /host/tests/regress/boot/guest.sh $name '$app' $settle" 1 >/dev/null
if ! timeout 900 sh -c 'until grep -q "^BOOTTEST done" build/console.log; do sleep 2; done'; then
	echo "no result within 900 s" >&2
fi
grep -a '^BOOTTEST' build/console.log | tr -d '\r' > "build/boot-times/$name.out"
LOG_TAG=$name-stop SAVE_LOGS=0 ./host/scripts/vm-launcher stop >/dev/null
cat "build/boot-times/$name.out"
if [ -d "build/logs/$name" ]; then
	python3 tests/regress/boot/timeline.py "build/logs/$name" ${app:+--app "$app"} --json | tee "build/boot-times/$name.json"
fi
