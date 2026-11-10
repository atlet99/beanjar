#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Offline integration test only (link=off): export just the native device-ID header, never the session.
set -eu
case " $(cat /proc/cmdline) " in *' webos.net=off '*) ;; *) echo 'FAIL link=off required'; exit 1;; esac
umask 077
out=/mnt/host/out/sdp-auth/native-device-id
mkdir -p /mnt/host/out/sdp-auth
headers=$(timeout -t 10 luna-send -n 1 luna://com.webos.service.sdx/getHttpHeaderForServiceRequest '{}' 2>/dev/null)
printf %s "$headers" | sed -n 's/.*"X-Device-ID"[ ]*:[ ]*"\([^"]*\)".*/\1/p' > "$out"
[ -s "$out" ] || { echo 'FAIL no native device ID'; exit 1; }
chmod 600 "$out"
echo 'NATIVE-DEVICE-ID-READY (value omitted)'
