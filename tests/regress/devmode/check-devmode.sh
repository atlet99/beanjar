#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Developer mode check (guest/layers/devmode), run on the QEMU host against a running VM with
# LAYERS=...,devmode. It does what the ares tools do on a TV in developer mode, with plain
# ssh/sftp/curl, so it needs no ares install (but ares-package, for the test app):
#   1. key server: GET http://HOST:9991/webos_rsa returns a PEM key encrypted with AES-128-CBC;
#      other paths 404. The key opens with the guest's passphrase (ares-novacom --getkey), which is
#      com.palm.service.devmode/getPassphrase's answer (a TV's rule: its NDUID's first six
#      characters), asked as the Developer Mode app over the root serial console (host/scripts/vm-launcher
#      console), and it is the VM's developer key, identity/devmode_rsa
#   2. ssh -p 9922 prisoner@HOST with that key: the host key is identity/known_hosts', uid
#      prisoner, HOME /media/developer/, the jail's environment; root cannot log in, and the
#      server offers only publickey
#   3. com.palm.service.devmode/getDevMode (luna-send-pub, from the jail) -> enabled
#   4. ares-install: sftp upload of a test ipk (tests/regress/devmode/app/, copied to build/test-apps/ with
#      APP_ID and packed by ares-package) to
#      /media/developer/temp/ares_install_<sha>.ipk, sha256sum, dev/install until "installed"
#   5. ares-install -l / ares-launch: dev/listApps has the app, launch, dev/running has it;
#      GRAB=file.png also takes a screenshot through the dbus display (tests/regress/virgl/dbus-grab.py,
#      DISPLAY_BACKEND=dbus), then dev/closeByAppId
#   6. ares-install -r: dev/remove until "removed", and dev/listApps no longer has the app
#
#   tests/regress/devmode/check-devmode.sh [VM_DIR]
#       VM_DIR: a library VM's directory (its identity/ and serial console); without it the
#       checkout's VM (local/, build/console.sock).
# Also checks same-version reinstall. SSH commands and Luna calls use no PTY.
# Env: NATIVE_BINARY (optional SDK-built native-test.c, otherwise a web app),
#      HOST (default the VM's host address, 127.0.1.<its slot>: `host/scripts/vm-launcher config ADDRESS`), APP_ID (default com.example.devmodetest), GRAB, GRAB_WAIT
#      (seconds between launch and screenshot, default 5), CONSOLE_WAIT (seconds the getPassphrase
#      console line may take, default 5; more under TCG)
set -u
TOP=$(cd "$(dirname "$0")/../../.." && pwd)
VM_DIR=${1:-}
if [ -n "$VM_DIR" ]; then
	IDENT=$VM_DIR/identity RUN=$VM_DIR/run
	console() { "$TOP/host/scripts/vm-launcher" console --dir "$VM_DIR" "$@"; }
else
	IDENT=$TOP/local RUN=$TOP/build
	console() { "$TOP/host/scripts/vm-launcher" console "$@"; }
fi
KEY=$IDENT/devmode_rsa SOCK=$RUN/console.sock
if [ -z "${HOST:-}" ]; then
	HOST=$("$TOP/host/scripts/vm-launcher" config ADDRESS ${VM_DIR:+--dir "$VM_DIR"}) || exit 1
fi
APP_ID=${APP_ID:-com.example.devmodetest}
CONSOLE_WAIT=${CONSOLE_WAIT:-5}
NATIVE_BINARY=${NATIVE_BINARY:-}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# ssh as ares does: key only, no host key pinning (ares-cli-rs does not check it either).
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=10"
dm() { timeout 150 ssh $O -o BatchMode=yes -i "$T/key" -p 9922 prisoner@"$HOST" "$@"; }
# A subscription (luna-send-pub -i, as ares does) inside the jail, until its output matches
# $3 or 120 s pass (busybox sleep takes whole seconds); prints the output. luna-send stops at stdin
# EOF, hence </dev/null there.
subscribe() {
	dm "luna-send-pub -i '$1' '$2' </dev/null >/tmp/sub.out 2>&1 & p=\$!
		n=0; until grep -q -E '$3' /tmp/sub.out || [ \$n -ge 120 ]; do sleep 1; n=\$((n+1)); done
		kill \$p; cat /tmp/sub.out; rm -f /tmp/sub.out"
}

# 1. key server, and the guest's passphrase
code=$(curl --max-time 10 -s -o "$T/served" -w '%{http_code}' "http://$HOST:9991/webos_rsa")
if [ "$code" = 200 ] && head -n 3 "$T/served" | grep -q '^Proc-Type: 4,ENCRYPTED' &&
	head -n 3 "$T/served" | grep -q '^DEK-Info: AES-128-CBC,'; then
	pass "key server: GET /webos_rsa 200, a PEM key encrypted with AES-128-CBC"
else fail "key server: GET /webos_rsa -> $code ($(head -n 2 "$T/served" 2>/dev/null | tr '\n' ' '))"; fi
code=$(curl --max-time 10 -s -o "$T/not-found" -w '%{http_code}' "http://$HOST:9991/other")
curl_status=$?
if [ "$curl_status" = 0 ] && [ "$code" = 404 ] && [ "$(wc -c < "$T/not-found")" -eq 9 ]; then
	pass "key server: other paths 404, complete 9-byte body"
else fail "key server: /other -> $code, curl exit $curl_status"; fi
PASS=
if [ -S "$SOCK" ]; then
	# through the VM's output share (/mnt/host/out, RUN/share/out here)
	rm -f "$RUN/share/out/devmode-passphrase.json"
	console "webos-enter sh -c 'luna-send -n 1 -a com.palmdts.devmode luna://com.palm.service.devmode/getPassphrase {} </dev/null >/mnt/host/out/devmode-passphrase.json'" "$CONSOLE_WAIT" >/dev/null
	PASS=$(sed -n 's/.*"passphrase" *: *"\([^"]*\)".*/\1/p' "$RUN/share/out/devmode-passphrase.json" 2>/dev/null)
	rm -f "$RUN/share/out/devmode-passphrase.json"
	[ -n "$PASS" ] && pass "getPassphrase answers $PASS" || fail "getPassphrase: no passphrase"
else fail "no $SOCK: cannot ask the guest's getPassphrase"; fi
cp "$T/served" "$T/key"; chmod 600 "$T/key"
if [ -n "$PASS" ] && ssh-keygen -q -p -P "$PASS" -N '' -f "$T/key" >/dev/null 2>&1 &&
	[ "$(ssh-keygen -y -f "$T/key" | cut -d' ' -f1,2)" = "$(cut -d' ' -f1,2 "$KEY.pub")" ]; then
	pass "served key opens with getPassphrase's $PASS and is $KEY"
else fail "served key does not open with '$PASS' or is not $KEY"; cp "$KEY" "$T/key"; chmod 600 "$T/key"; fi
if timeout 30 ssh -o UserKnownHostsFile="$IDENT/known_hosts" -o StrictHostKeyChecking=yes -o LogLevel=ERROR \
	-o ConnectTimeout=10 -o BatchMode=yes -i "$T/key" -p 9922 prisoner@"$HOST" true </dev/null; then
	pass "host key on 9922 is the one in $IDENT/known_hosts"
else fail "host key on 9922 does not match $IDENT/known_hosts"; fi

# 2. prisoner in the jail
out=$(dm 'id -un; id -u; echo "$HOME"; echo "$PATH"; echo "$APPID"; hostname; touch /media/developer/x 2>/dev/null && echo home-writable; touch /media/developer/temp/.w && rm /media/developer/temp/.w && echo temp-writable' </dev/null)
echo "$out" | sed 's/^/  /'
set -- $out
# HOME has no trailing slash here: the stock jail conf in every inspected
# firmware (k7lp, lm21a, k8hp-mullet) sets `setenv HOME /media/developer` and
# `homedir /media/developer`, and the live jail agrees. Accept both forms.
home=${3:-}; home=${home%/}
if [ "${1:-}" = prisoner ] && [ "${2:-0}" != 0 ] && [ "$home" = /media/developer ] && [ "${5:-}" = com.palm.devmode.openssh ] &&
	! echo "$out" | grep -q home-writable && echo "$out" | grep -q temp-writable; then
	pass "ssh prisoner@$HOST:9922: uid $2, HOME $3, jail $5, only temp/ writable"
else fail "ssh prisoner: unexpected session: $out"; fi
if ssh $O -o BatchMode=yes -i "$T/key" -p 9922 root@"$HOST" true </dev/null 2>/dev/null; then fail "root login accepted"
else pass "root login refused"; fi
methods=$(ssh $O -o BatchMode=yes -o PreferredAuthentications=none -p 9922 nobody@"$HOST" true </dev/null 2>&1 |
	sed -n 's/.*Permission denied (\(.*\)).*/\1/p')
[ "$methods" = publickey ] && pass "auth methods offered: $methods" || fail "auth methods offered: $methods"

# 3. getDevMode
# One retry: once, the first call after a boot got "Message status unknown" from the hub (the
# service is a dynamic node service); the next calls answered in 0.4 s.
# busybox up to 1.29 only takes `timeout -t SECS`, 1.31 and later only `timeout SECS`.
getdevmode='if timeout -t 1 true 2>/dev/null; then t="timeout -t"; else t=timeout; fi
$t 20 luna-send-pub -n 1 luna://com.palm.service.devmode/getDevMode {} </dev/null'
r=$(dm "$getdevmode" </dev/null)
if ! echo "$r" | grep -q '"enabled":true'; then
	echo "  getDevMode, first try: $r"
	r=$(dm "$getdevmode" </dev/null)
fi
echo "$r" | grep -q '"enabled":true' && pass "getDevMode: $r" || fail "getDevMode: $r"

# 4. install, as ares-install
A=$TOP/build/test-apps/$APP_ID
rm -rf "$A"; mkdir -p "$TOP/build/test-apps"; cp -r "$TOP/tests/regress/devmode/app" "$A"
if [ -n "$NATIVE_BINARY" ]; then
    cp "$NATIVE_BINARY" "$A/native-test"
    jq --arg id "$APP_ID" '.id=$id | .type="native" | .main="native-test"' "$TOP/tests/regress/devmode/app/appinfo.json" > "$A/appinfo.json"
    dm 'rm -f /media/developer/temp/devmode-native-status' </dev/null
else
    jq --arg id "$APP_ID" '.id=$id' "$TOP/tests/regress/devmode/app/appinfo.json" > "$A/appinfo.json"
fi
mkdir -p "$T/pkg"; ares-package -o "$T/pkg" "$A" >/dev/null || { fail "ares-package $A"; exit 1; }
mv "$T"/pkg/*.ipk "$T/app.ipk"
sum=$(sha256sum "$T/app.ipk" | cut -d' ' -f1)
ipk=/media/developer/temp/ares_install_$(printf %s "$sum" | cut -c1-10).ipk
printf 'put %s %s\n' "$T/app.ipk" "$ipk" | sftp $O -o BatchMode=yes -i "$T/key" -P 9922 -b - prisoner@"$HOST" >/dev/null 2>&1
rsum=$(dm "sha256sum $ipk" </dev/null | cut -d' ' -f1)
[ "$rsum" = "$sum" ] && pass "sftp upload to $ipk, sha256 matches" || fail "sftp upload: sha256 '$rsum' != '$sum'"
for attempt in install reinstall; do
	r=$(subscribe luna://com.webos.appInstallService/dev/install \
		"{\"id\":\"com.ares.defaultName\",\"ipkUrl\":\"$ipk\",\"subscribe\":true}" '"state":"(installed|install failed)"|"returnValue":false')
	echo "$r" | sed -n 's/.*"state":"\([^"]*\)".*/  install state: \1/p' | uniq
	echo "$r" | grep -q '"state":"installed"' && pass "dev/$attempt: installed" || fail "dev/$attempt: $(echo "$r" | tail -n 1)"
done
dm "rm -f $ipk" </dev/null

# 5. list, launch, running, close
r=$(dm 'luna-send-pub -n 1 luna://com.webos.applicationManager/dev/listApps {} </dev/null')
echo "$r" | grep -q "\"id\":\"$APP_ID\"" && pass "dev/listApps has $APP_ID" || fail "dev/listApps lacks $APP_ID"
r=$(dm "luna-send-pub -n 1 luna://com.webos.applicationManager/launch '{\"id\":\"$APP_ID\"}' </dev/null")
echo "$r" | grep -q '"returnValue":true' && pass "launch: $r" || fail "launch: $r"
sleep "${GRAB_WAIT:-5}"
r=$(dm 'luna-send-pub -n 1 luna://com.webos.applicationManager/dev/running {} </dev/null')
echo "$r" | grep -q "\"id\":\"$APP_ID\"" && pass "dev/running has $APP_ID" || fail "dev/running: $r"
if [ -n "$NATIVE_BINARY" ]; then
    r=$(dm 'cat /media/developer/temp/devmode-native-status' </dev/null)
    echo "$r" | grep -q '^NATIVE-PASS uid=[1-9][0-9]* pid=[1-9][0-9]*$' &&
        pass "native executable ran as prisoner: $r" || fail "native executable: $r"
fi
if [ -n "${GRAB:-}" ]; then
	python3 "$TOP/tests/regress/virgl/dbus-grab.py" --bus "unix:path=$RUN/dbus.sock" "$GRAB" 3 >/dev/null 2>&1 &&
		pass "screenshot: $GRAB" || fail "screenshot failed"
fi
r=$(dm "luna-send-pub -n 1 luna://com.webos.applicationManager/dev/closeByAppId '{\"id\":\"$APP_ID\"}' </dev/null")
echo "$r" | grep -q '"returnValue":true' && pass "dev/closeByAppId: $r" || fail "dev/closeByAppId: $r"

# 6. remove, as ares-install -r
r=$(subscribe luna://com.webos.appInstallService/dev/remove "{\"id\":\"$APP_ID\",\"subscribe\":true}" \
	'"state":"(removed|remove failed)"|"returnValue":false')
echo "$r" | grep -q '"state":"removed"' && pass "dev/remove: removed" || fail "dev/remove: $(echo "$r" | tail -n 1)"
r=$(dm 'luna-send-pub -n 1 luna://com.webos.applicationManager/dev/listApps {} </dev/null')
echo "$r" | grep -q "\"id\":\"$APP_ID\"" && fail "dev/listApps still has $APP_ID" || pass "dev/listApps no longer has $APP_ID"

if [ -n "$NATIVE_BINARY" ]; then
    dm 'rm -f /media/developer/temp/devmode-native-status' </dev/null
fi
if [ "$fails" = 0 ]; then echo "DEVMODE PASS"; else echo "DEVMODE FAIL ($fails)"; exit 1; fi
