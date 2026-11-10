#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Real SDP authentication with the provisioned model key: needs
# a supplied model item in the VM's identity/sestore.toml and link=nat.
# Print results, never identities,
# session tokens, cookies, or bodies.
set -eu
case " $(cat /proc/cmdline) " in *' webos.net=direct '*|*' webos.net=mitm '*) ;; *) echo 'FAIL requires link=nat'; exit 1;; esac
[ -c /dev/lg/beanjar-sestore ] || { echo 'FAIL secure-store device missing'; exit 1; }
[ -f /etc/emulator/sestore.bin ] || { echo 'FAIL provisioning bundle missing'; exit 1; }
[ "$(stat -c %a /etc/emulator/sestore.bin)" = 600 ] || { echo 'FAIL bundle permissions'; exit 1; }
luna() { timeout -t 10 luna-send -n 1 "$@" </dev/null 2>/dev/null | tr -d ' \r\n'; }
SDX=luna://com.webos.service.sdx
elapsed=0
while :; do
    status=$(luna "$SDX/getDeviceAuthenticationStatus" '{}' || true)
    case $status in *'"isDeviceAuthenticated":true'*) break;; esac
    [ "$elapsed" -lt "${1:-90}" ] || { echo 'FAIL native sdx not authenticated'; exit 1; }
    sleep 3
    elapsed=$((elapsed + 3))
done
headers=$(luna "$SDX/getHttpHeaderForServiceRequest" '{}' || true)
case $headers in *'"X-Authentication":"emulator-session"'*|*'"X-Authentication":"(null)"'*)
    echo 'FAIL native sdx has a synthetic or empty session'; exit 1;; esac
printf %s "$headers" | grep -q '"X-Authentication":"[^"][^"]*"' ||
    { echo 'FAIL session header missing'; exit 1; }
echo 'PASS secure-store provisioning: device and owner-only bundle'
echo 'PASS native sdx: authenticated with non-stub session'
echo 'SDP-REAL-AUTH PASS'
