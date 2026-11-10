#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Developer mode key server (guest/layers/devmode): wait, at most WAIT seconds, for the developer
# key start-devmode.sh writes, before the key server starts serving it. The devmode job counts as
# started (Upstart's started event, systemd's Type=simple) as soon as start-devmode.sh runs, before
# it has written the key, and an early `ares-novacom --getkey` would get a 404. Past the limit
# (no NDUID, openssl failed) the server starts anyway and answers 404 as before; busybox sh.
KEY=${KEY:-/var/luna/preferences/webos_rsa}
WAIT=${1:-30}

i=0
while ! [ -s "$KEY" ]; do
	if [ "$i" -ge "$WAIT" ]; then
		echo "devmode: no developer key at $KEY after $WAIT s; serving without it" >&2
		exit 0
	fi
	sleep 1
	i=$((i + 1))
done
