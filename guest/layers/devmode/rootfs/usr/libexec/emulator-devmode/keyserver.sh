#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Developer mode key server (guest/layers/devmode), one connection per run under busybox tcpsvd
# (emulator-devmode-keyserver.conf). Like the Developer Mode app's KeyServer on port 9991, it
# answers GET /webos_rsa with the developer key, still encrypted with its passphrase, which
# `ares-novacom --getkey` fetches, and HEAD /webos_rsa with the same headers (@webos-tools/cli
# 3.x asks HEAD first and gives up on anything but 200). Everything else is 404.
KEY=/var/luna/preferences/webos_rsa

read -r method path _ || exit 0
# Drain the request headers (up to the blank line), so the client never sees a reset.
while read -r -t 5 line; do
	[ -z "$(printf %s "$line" | tr -d '\r')" ] && break
done
path=$(printf %s "$path" | tr -d '\r')

if { [ "$method" = GET ] || [ "$method" = HEAD ]; } && [ "$path" = /webos_rsa ] && [ -r "$KEY" ]; then
	printf 'HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %s\r\nConnection: close\r\n\r\n' \
		"$(wc -c < "$KEY")"
	[ "$method" = GET ] && cat "$KEY"
else
	printf 'HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found\n'
fi
